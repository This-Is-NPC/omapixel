// The core's tests.
//
// They cover the model and not the front ends, because the model is where the
// behaviour is: the CLI and the studio are argument parsing and pixels on
// screen over the same functions. A rule tested here is a rule both obey, which
// is the property the C++ rewrite exists to buy -- before, `resize` lived twice
// and only one copy had a test.

#include <QtTest>
#include <QCommandLineParser>
#include <QClipboard>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QQuickView>
#include <qpa/qwindowsysteminterface.h>
#include <QProcess>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSet>
#include <QJsonArray>

#include <limits>
#include <QJsonDocument>

#include "Bridge.h"
#include "Codec.h"
#include "Document.h"
#include "Differences.h"
#include "Grid.h"
#include "Ops.h"
#include "Palette.h"
#include "Render.h"
#include "Sessions.h"
#include "Commands.h"
#include "Config.h"
#include "Strings.h"
#include "Toml.h"
#include "DocumentModel.h"
#include "InputLog.h"
#include "PixelGridItem.h"
#include "SessionPublisher.h"
#include "ChangeLog.h"
#include "Theme.h"

using namespace omapixel;

namespace {

/// A small readable document: a 4x3 clip with a cross in it.
Document sample()
{
    Document doc = Document::blank(4, 3);
    Grid grid = Grid::fromRows({QStringLiteral(".II."), QStringLiteral("IIII"),
                                QStringLiteral(".II.")});
    doc.setFrame(QStringLiteral("idle"), 0, grid);
    return doc;
}

/// A worst-case Full HD document: every cell is drawn, so opening validates
/// every cell and rendering cannot skip transparent pixels. Extra frames are
/// duplicates because their content is irrelevant to the amount of work.
Document fullHdDocument(int frameCount)
{
    Document doc = Document::blank(1920, 1080);
    Grid frame(1920, 1080, u'I');
    doc.setFrame(QStringLiteral("idle"), 0, frame);
    for (int index = 1; index < frameCount; ++index)
        doc.addFrame(QStringLiteral("idle"), index - 1, true);
    return doc;
}

/// A minimal catalog in the shape omagotchi uses.
QJsonObject catalog()
{
    const QJsonArray frame{QStringLiteral("..II.."), QStringLiteral("SSIISS"),
                           QStringLiteral("..II..")};
    QJsonArray frames{frame, frame};

    QJsonObject bank;
    QJsonObject states;
    states.insert(QStringLiteral("idle"), frames);
    QJsonObject fat;
    fat.insert(QStringLiteral("0"), states);
    bank.insert(QStringLiteral("fat"), fat);
    bank.insert(QStringLiteral("spawn"), QJsonArray{frame});
    bank.insert(QStringLiteral("boom"), QJsonArray{frame});
    for (const QString &name : {QStringLiteral("launch"), QStringLiteral("land"),
                                QStringLiteral("bye")}) {
        QJsonObject byVariant;
        byVariant.insert(QStringLiteral("0"), QJsonArray{frame});
        bank.insert(name, byVariant);
    }

    QJsonObject species;
    species.insert(QStringLiteral("critter"), bank);
    QJsonObject palette;
    palette.insert(QStringLiteral("I"), QStringLiteral("#1A1B26"));
    palette.insert(QStringLiteral("S"), QStringLiteral("#F0CDBF"));
    QJsonObject declared;
    declared.insert(QStringLiteral("idle"), 2);

    QJsonObject root;
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("states"), declared);
    root.insert(QStringLiteral("species"), species);
    return root;
}

/// Writes a theme into a scratch state directory, the way omarchy lays one out.
void writeTheme(const QString &state, const QString &name, const QString &mode,
                const QString &background, const QString &accent,
                const QString &foreground)
{
    const QString dir = state + QStringLiteral("/omarchy/themes/") + name;
    QDir().mkpath(dir);
    QFile colours(dir + QStringLiteral("/colors.toml"));
    colours.open(QIODevice::WriteOnly);
    colours.write(QStringLiteral("mode = \"%1\"\naccent = \"%2\"\n"
                                 "background = \"%3\"\nforeground = \"%4\"\n"
                                 "red = \"#c38b7b\"\nmuted = \"#584e51\"\n")
                      .arg(mode, accent, background, foreground)
                      .toUtf8());
    colours.close();
    QDir().mkpath(state + QStringLiteral("/omarchy/current"));
}

/// Repoints the symlink, which is what `omarchy theme set` does.
void pointAt(const QString &state, const QString &name)
{
    const QString link = state + QStringLiteral("/omarchy/current/theme");
    QFile::remove(link);
    QFile::link(state + QStringLiteral("/omarchy/themes/") + name, link);
}

/// Writes a document straight to disk the way the CLI writes: through the same
/// atomic rename. Watcher tests only mean something if the write shape is the
/// real one.
bool writeDocument(const QString &path, const Document &document)
{
    QString error;
    return Codec::writeFile(path, document, &error);
}

} // namespace

/// `undo` is a slot on the model; calling it from a test needs no ceremony,
/// but naming it here keeps the key tests readable.
void doc_undo(omapixel::DocumentModel *model)
{
    model->undo();
}

class OmapixelTest : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // The tests must not read the config file of whoever is running them:
        // a rebound key on this machine would fail a test that passes
        // everywhere else. Pointed at a path that is not there, which is the
        // supported way of saying "the defaults".
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config::shared().load();

        // And the catalogue, because the window reads every one of its labels
        // out of it. Without this the QML tests ran with `T` undefined and
        // every string in the window empty -- which they survived only
        // because a QML binding error is a warning.
        Strings::shared().load(QStringLiteral("en"));
    }


    // ------------------------------------------------------------------ Grid

    void gridReadsOutOfBoundsAsEmpty()
    {
        // Every drawing tool works near an edge. Making each caller bounds
        // check is how one of them eventually forgets.
        const Grid grid(2, 2, u'I');
        QCOMPARE(grid.at(0, 0), QChar(u'I'));
        QCOMPARE(grid.at(-1, 0), Grid::Empty);
        QCOMPARE(grid.at(0, 99), Grid::Empty);
    }

    void gridPadsShortRowsInsteadOfRefusingThem()
    {
        // A hand-written file with a short line has to open so it can be fixed.
        const Grid grid = Grid::fromRows({QStringLiteral("IIII"),
                                          QStringLiteral("II")});
        QCOMPARE(grid.columns(), 4);
        QCOMPARE(grid.row(1), QStringLiteral("II.."));
    }

    void gridCountsWhatIsDrawn()
    {
        QCOMPARE(sample().frame(QStringLiteral("idle"), 0).drawnCount(), 8);
    }

    // --------------------------------------------------------------- Palette

    void paletteKeepsTheOrderItWasGiven()
    {
        // The order is the order the swatch strip draws in, so it is content.
        // A palette that reshuffles itself on save is a palette whose colours
        // move under the cursor.
        Palette palette;
        palette.set(u'Z', QColor("#111111"));
        palette.set(u'A', QColor("#222222"));
        palette.set(u'M', QColor("#333333"));
        QCOMPARE(palette.letters(), (QList<QChar>{u'Z', u'A', u'M'}));
    }

    void paletteRecoloursInPlace()
    {
        Palette palette;
        palette.set(u'A', QColor("#111111"));
        palette.set(u'B', QColor("#222222"));
        palette.set(u'A', QColor("#333333"));
        QCOMPARE(palette.size(), 2);
        QCOMPARE(palette.letters().first(), QChar(u'A'));
        QCOMPARE(palette.colour(u'A').name().toUpper(), QStringLiteral("#333333"));
    }

    // -------------------------------------------------------------- Document

    void aClipKeepsItsLastFrame()
    {
        // A clip with no frames cannot be drawn. Deleting the clip is a
        // different command, and it is spelled out.
        Document doc = Document::blank(4, 3);
        QVERIFY(!doc.removeFrame(QStringLiteral("idle"), 0));
        doc.addFrame(QStringLiteral("idle"), 0, false);
        QVERIFY(doc.removeFrame(QStringLiteral("idle"), 0));
        QCOMPARE(doc.clip(QStringLiteral("idle"))->frames.size(), 1);
    }

    void renamingKeepsTheClipWhereItWas()
    {
        // The list is the sidebar. A clip that jumps to the end when renamed
        // looks like another clip.
        Document doc = Document::blank(4, 3);
        doc.addClip(QStringLiteral("walk"));
        doc.addClip(QStringLiteral("jump"));
        QVERIFY(doc.renameClip(QStringLiteral("walk"), QStringLiteral("run")));
        QCOMPARE(doc.clipNames(), (QStringList{QStringLiteral("idle"),
                                               QStringLiteral("run"),
                                               QStringLiteral("jump")}));
    }

    void aDuplicateClipNameIsRefused()
    {
        Document doc = Document::blank(4, 3);
        QVERIFY(!doc.addClip(QStringLiteral("idle")));
        QVERIFY(!doc.renameClip(QStringLiteral("idle"), QStringLiteral("idle")));
    }

    void aFrameOfTheWrongSizeIsRefused()
    {
        // Otherwise the document ends up with frames of two sizes, which is a
        // file nothing can draw.
        Document doc = Document::blank(4, 3);
        QVERIFY(!doc.setFrame(QStringLiteral("idle"), 0, Grid(2, 2)));
    }

    // ---------------------------------------------------------------- resize

    void growingCentresTheDrawingAndLosesNothing()
    {
        Document doc = Document::blank(2, 2);
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("II"), QStringLiteral("II")}));
        QCOMPARE(doc.wouldLose(4, 4), 0);
        doc.resize(4, 4);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).toRows(),
                 (QStringList{QStringLiteral("...."), QStringLiteral(".II."),
                              QStringLiteral(".II."), QStringLiteral("....")}));
    }

    void growingThenShrinkingBackReturnsTheOriginal()
    {
        // Growing is not destructive, so undoing it by shrinking has to hand
        // the drawing back. It is the least that "centred" promises.
        const QStringList start{QStringLiteral("I.I"), QStringLiteral(".I."),
                                QStringLiteral("I.I")};
        Document doc = Document::blank(3, 3);
        doc.setFrame(QStringLiteral("idle"), 0, Grid::fromRows(start));
        doc.resize(9, 9);
        doc.resize(3, 3);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).toRows(), start);
    }

    void shrinkingCountsWhatItWouldCutBeforeCutting()
    {
        Document doc = Document::blank(4, 4);
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("IIII"), QStringLiteral("IIII"),
                                     QStringLiteral("IIII"), QStringLiteral("IIII")}));
        QCOMPARE(doc.wouldLose(2, 2), 12);
        doc.resize(2, 2);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).drawnCount(), 4);
    }

    void resizeReachesEveryFrameOfEveryClip()
    {
        Document doc = Document::blank(2, 2);
        doc.addClip(QStringLiteral("walk"));
        doc.addFrame(QStringLiteral("walk"), 0, false);
        doc.resize(6, 5);
        for (const Clip &clip : doc.clips()) {
            for (const Grid &grid : clip.frames) {
                QCOMPARE(grid.columns(), 6);
                QCOMPARE(grid.rows(), 5);
            }
        }
    }

    void drawnBoundsIgnoreOuterEmptinessButKeepInternalGaps()
    {
        Document doc = Document::blank(8, 6);
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("........"),
                                     QStringLiteral("..I....."),
                                     QStringLiteral("........"),
                                     QStringLiteral("....I..."),
                                     QStringLiteral("........"),
                                     QStringLiteral("........")}));

        QCOMPARE(doc.drawnBounds(QStringLiteral("idle"), 0), QRect(2, 1, 3, 3));
        QVERIFY(doc.crop(QRect(2, 1, 3, 3)));
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).toRows(),
                 (QStringList{QStringLiteral("I.."), QStringLiteral("..."),
                              QStringLiteral("..I")}));
    }

    void asymmetricCropReachesEveryFrameAndCountsOutsidePixels()
    {
        Document doc = Document::blank(8, 6);
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("........"),
                                     QStringLiteral("..I.I..."),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........")}));
        doc.addClip(QStringLiteral("walk"));
        doc.setFrame(QStringLiteral("walk"), 0,
                     Grid::fromRows({QStringLiteral(".......I"),
                                     QStringLiteral("...I...."),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........")}));

        const QRect kept = doc.drawnBounds(QStringLiteral("idle"), 0);
        QCOMPARE(kept, QRect(2, 1, 3, 1));
        QCOMPARE(doc.wouldLoseOutside(kept), 1);
        QVERIFY(doc.crop(kept));
        QCOMPARE(doc.columns(), 3);
        QCOMPARE(doc.rows(), 1);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).toRows(),
                 (QStringList{QStringLiteral("I.I")}));
        QCOMPARE(doc.frame(QStringLiteral("walk"), 0).toRows(),
                 (QStringList{QStringLiteral(".I.")}));
    }

    void anEmptyFrameHasNoTrimBounds()
    {
        Document doc = Document::blank(8, 6);
        QVERIFY(!doc.drawnBounds(QStringLiteral("idle"), 0).isValid());
        QVERIFY(!doc.crop(QRect()));
        QCOMPARE(doc.columns(), 8);
        QCOMPARE(doc.rows(), 6);
    }

    // -------------------------------------------------------------- problems

    void aSoundDocumentHasNothingToSay()
    {
        QVERIFY(sample().problems().isEmpty());
    }

    void aSlotWithoutAColourIsNamed()
    {
        // The renderer skips a letter it has no colour for, so this breaks
        // nothing -- it just loses those pixels, quietly, on every surface.
        // That silence is why it is worth reporting.
        Document doc = sample();
        doc.palette().remove(u'I');
        const QStringList problems = doc.problems();
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QLatin1String("I")));
    }

    void theTransparentSlotMayNotBeColoured()
    {
        Document doc = sample();
        doc.palette().set(Grid::Empty, QColor("#000000"));
        QVERIFY(!doc.problems().isEmpty());
    }

    // ----------------------------------------------------------------- Codec

    void aDocumentSurvivesTheRoundTrip()
    {
        const Document before = sample();
        const Codec::Result after = Codec::read(Codec::write(before));
        QVERIFY2(after.ok, qPrintable(after.error));
        QVERIFY(after.document == before);
    }

    void theRoundTripKeepsThePaletteOrder()
    {
        // This is the reason palette and clips are arrays in the format. Qt's
        // QJsonObject sorts its keys, so an object-shaped palette comes back
        // reordered -- and the order is what the strip shows.
        Document doc = Document::blank(2, 2);
        doc.palette() = Palette();
        doc.palette().set(u'Z', QColor("#111111"));
        doc.palette().set(u'A', QColor("#222222"));
        const Codec::Result back = Codec::read(Codec::write(doc));
        QVERIFY(back.ok);
        QCOMPARE(back.document.palette().letters(), (QList<QChar>{u'Z', u'A'}));
    }

    void theLegacyObjectShapeStillOpens()
    {
        // Documents written by the Python version. Nobody's file should stop
        // opening because the program grew up.
        const QByteArray legacy = R"({
            "size": {"w": 2, "h": 2},
            "palette": {"I": "#1A1B26"},
            "clips": {"idle": {"fps": 6, "frames": [["II", "I."]]}}
        })";
        const Codec::Result read = Codec::read(legacy);
        QVERIFY2(read.ok, qPrintable(read.error));
        QCOMPARE(read.document.clipNames(), QStringList{QStringLiteral("idle")});
        QCOMPARE(read.document.clip(QStringLiteral("idle"))->fps, 6);
        QCOMPARE(read.document.frame(QStringLiteral("idle"), 0).row(1),
                 QStringLiteral("I."));
    }

    void badJsonIsAMessageAndNotACrash()
    {
        const Codec::Result read = Codec::read(QByteArray("{not json"));
        QVERIFY(!read.ok);
        QVERIFY(!read.error.isEmpty());
    }

    void aMissingSizeIsRefused()
    {
        const Codec::Result read = Codec::read(QByteArray(R"({"clips": []})"));
        QVERIFY(!read.ok);
        QVERIFY(read.error.contains(QLatin1String("size")));
    }

    void malformedDocumentsAreRefusedBeforeTheyCanBeNormalised()
    {
        const QByteArray valid = R"({
            "size": {"w": 1, "h": 1},
            "palette": [{"slot": "I", "colour": "#112233"}],
            "clips": [{"name": "idle", "fps": 8, "frames": [["I"]]}]
        })";
        QVERIFY(Codec::read(valid).ok);

        QByteArray wrongType = valid;
        wrongType.replace("\"fps\": 8", "\"fps\": \"8\"");
        QVERIFY(Codec::read(wrongType).error.contains(QLatin1String(".fps")));

        QByteArray shortRow = valid;
        shortRow.replace("[\"I\"]", "[\"\"]");
        QVERIFY(Codec::read(shortRow).error.contains(QLatin1String("QChars")));

        QByteArray unknownSlot = valid;
        unknownSlot.replace("[\"I\"]", "[\"Z\"]");
        QVERIFY(Codec::read(unknownSlot).error.contains(QLatin1String("undefined")));

        const QByteArray duplicate = R"({
            "size": {"w": 1, "w": 2, "h": 1},
            "palette": [{"slot": "I", "colour": "#112233"}],
            "clips": [{"name": "idle", "fps": 8, "frames": [["I"]]}]
        })";
        QVERIFY(Codec::read(duplicate).error.contains(QLatin1String("duplicate")));
    }

    void resourceThresholdsWarnWithoutRefusingTheDocument()
    {
        Codec::WarningLimits limits;
        limits.fileBytes = 1;
        limits.clips = 0;
        limits.framesPerClip = 0;
        limits.totalFrames = 0;
        limits.paletteSlots = 0;
        const Codec::Result read = Codec::read(Codec::write(sample()), limits);
        QVERIFY(read.ok);
        QCOMPARE(read.warnings.size(), 1);
        QVERIFY(read.warnings.first().contains(QLatin1String("threshold")));
    }

    void writingIsAtomic()
    {
        // The studio watches the file it has open. It must not read half a
        // document because a CLI command is halfway through a write.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("d.json"));
        QVERIFY(Codec::writeFile(path, sample()));
        const Codec::Result back = Codec::readFile(path);
        QVERIFY(back.ok);
        QVERIFY(back.document == sample());
    }

    void documentModelSavesFileDialogUrls()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("saved.json"));
        DocumentModel document;
        document.paint(0, 0, QStringLiteral("I"));
        QVERIFY(document.save(QUrl::fromLocalFile(path).toString()));
        QCOMPARE(document.path(), path);
        QVERIFY(Codec::readFile(path).ok);
    }

    // ------------------------------------------------------------------- Ops

    void lineLeavesNoGapsOnASteepSlope()
    {
        // Interpolating in floating point leaves holes on steep slopes, and on
        // a 24-row sprite a hole is something you can see.
        Grid grid(8, 8);
        ops::line(grid, QPoint(0, 0), QPoint(1, 7), u'I');
        for (int y = 0; y < 8; ++y)
            QVERIFY2(grid.row(y).contains(u'I'), qPrintable(QString::number(y)));
    }

    void rectDrawsAnOutlineOrABlock()
    {
        Grid outline(5, 5);
        ops::rect(outline, QPoint(0, 0), QPoint(4, 4), u'I', false);
        QCOMPARE(outline.drawnCount(), 16);

        Grid block(5, 5);
        ops::rect(block, QPoint(0, 0), QPoint(4, 4), u'I', true);
        QCOMPARE(block.drawnCount(), 25);
    }

    void extremeGeometryIsClippedBeforeItIsWalked()
    {
        Grid line(16, 16);
        QElapsedTimer elapsed;
        elapsed.start();
        ops::line(line, QPoint(std::numeric_limits<int>::min(), 8),
                  QPoint(std::numeric_limits<int>::max(), 8), u'I');
        QVERIFY(elapsed.elapsed() < 100);
        QCOMPARE(line.drawnCount(), 16);

        Grid filled(16, 16);
        ops::rect(filled,
                  QPoint(std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::min()),
                  QPoint(std::numeric_limits<int>::max(),
                         std::numeric_limits<int>::max()),
                  u'I', true);
        QCOMPARE(filled.drawnCount(), 16 * 16);

        Grid outline(16, 16);
        ops::rect(outline, QPoint(-100, -100), QPoint(100, 100), u'I', false);
        QCOMPARE(outline.drawnCount(), 0);
    }

    void fillStopsAtTheEdgeOfItsOwnRun()
    {
        Grid grid = Grid::fromRows({QStringLiteral("..I.."),
                                    QStringLiteral("..I.."),
                                    QStringLiteral("..I..")});
        ops::fill(grid, 0, 0, u'A');
        QCOMPARE(grid.row(0), QStringLiteral("AAI.."));
        QCOMPARE(grid.at(4, 0), Grid::Empty);
    }

    void fillOnALargeAreaDoesNotBlowTheStack()
    {
        // The reason it uses an explicit stack. Sixteen thousand pixels of one
        // colour is a stack overflow with recursion, not a bug report.
        Grid grid(128, 128);
        ops::fill(grid, 0, 0, u'I');
        QCOMPARE(grid.drawnCount(), 128 * 128);
    }

    void shiftDropsWhatLeavesTheFrame()
    {
        // No wrap. A sprite that wraps is a tiling pattern, and that is a
        // different tool.
        Grid grid = Grid::fromRows({QStringLiteral("II.."),
                                    QStringLiteral("....")});
        ops::shift(grid, 3, 0);
        QCOMPARE(grid.row(0), QStringLiteral("...I"));
    }

    void flipIsItsOwnInverse()
    {
        Grid grid = Grid::fromRows({QStringLiteral("IA.."),
                                    QStringLiteral(".B.C")});
        const Grid before = grid;
        ops::flipHorizontal(grid);
        QVERIFY(grid != before);
        ops::flipHorizontal(grid);
        QCOMPARE(grid, before);
    }

    void swapSlotReportsHowMuchItChanged()
    {
        Grid grid = Grid::fromRows({QStringLiteral("IIAA")});
        QCOMPARE(ops::swapSlot(grid, u'I', u'A'), 2);
        QCOMPARE(grid.row(0), QStringLiteral("AAAA"));
    }

    void diffNamesEveryPixelThatMoved()
    {
        Grid before = Grid::fromRows({QStringLiteral("II")});
        Grid after = Grid::fromRows({QStringLiteral("I.")});
        const auto differences = ops::diff(before, after);
        QCOMPARE(differences.size(), 1);
        QCOMPARE(differences.first().at, QPoint(1, 0));
    }

    // ---------------------------------------------------------------- Render

    void renderScalesAndLaysOutASheet()
    {
        Document doc = sample();
        doc.addFrame(QStringLiteral("idle"), 0, true);

        render::Options one;
        one.scale = 4;
        const QImage single = render::toImage(doc, QStringLiteral("idle"), 0, one);
        QCOMPARE(single.size(), QSize(16, 12));

        render::Options sheet;
        sheet.scale = 4;
        sheet.sheet = true;
        sheet.sheetGap = 2;
        const QImage strip = render::toImage(doc, QStringLiteral("idle"), 0, sheet);
        QCOMPARE(strip.width(), 16 * 2 + 2 * 4);
    }

    void renderPaintsTheSlotColourAndSkipsTheUnknown()
    {
        Document doc = sample();
        render::Options options;
        const QImage image = render::toImage(doc, QStringLiteral("idle"), 0, options);
        QCOMPARE(QColor(image.pixel(1, 0)), doc.palette().colour(u'I'));
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
    }

    void renderWarningsDoNotReplaceOverflowChecks()
    {
        Document doc = sample();
        doc.addFrame(QStringLiteral("idle"), 0, true);
        render::Options options;
        options.sheet = true;
        options.warningPixels = 1;
        QString warning;
        QString error;
        QVERIFY(!render::toImage(doc, QStringLiteral("idle"), 0, options,
                                 &warning, &error).isNull());
        QVERIFY(!warning.isEmpty());
        QVERIFY(error.isEmpty());

        options.sheetGap = std::numeric_limits<int>::max();
        QVERIFY(render::toImage(doc, QStringLiteral("idle"), 0, options,
                                &warning, &error).isNull());
        QVERIFY(error.contains(QLatin1String("limits"))
                || error.contains(QLatin1String("overflow")));
    }

    void textIsTheGridAndNothingElse()
    {
        // What a diff and a test want, and what survives being pasted anywhere.
        QCOMPARE(render::toText(sample(), QStringLiteral("idle"), 0),
                 QStringLiteral(".II.\nIIII\n.II.\n"));
    }

    void ansiSaysSomethingForADrawnFrame()
    {
        const QString ansi = render::toAnsi(sample(), QStringLiteral("idle"), 0);
        QVERIFY(!ansi.isEmpty());
        QVERIFY(ansi.contains(QStringLiteral("\x1b[")));
        // Two sprite rows per terminal row: three rows become two lines.
        QCOMPARE(ansi.count(QLatin1Char('\n')), 2);
    }

    // ---------------------------------------------------------------- Bridge

    void aCatalogSurvivesTheRoundTrip()
    {
        // Import and export without drawing anything has to hand the catalog
        // back unchanged. It is the property that lets you open somebody's art
        // without fear: if the round trip alters something, it alters it on its
        // own.
        const QJsonObject before = catalog();
        const Bridge::Result pulled =
            Bridge::importSpecies(before, QStringLiteral("critter"), QStringLiteral("0"));
        QVERIFY2(pulled.ok, qPrintable(pulled.error));
        const Bridge::Result pushed =
            Bridge::exportInto(before, pulled.document, QStringLiteral("critter"),
                               QStringLiteral("0"));
        QVERIFY(pushed.ok);
        QCOMPARE(pushed.catalog, before);
    }

    void everySequenceOfTheBankBecomesAClip()
    {
        // The sequences that do not live under `fat` are easy to forget, and
        // forgetting one means losing it on the way back.
        const Bridge::Result pulled =
            Bridge::importSpecies(catalog(), QStringLiteral("critter"),
                                  QStringLiteral("0"));
        QVERIFY(pulled.ok);
        // Held in a local: calling clipNames() twice makes two temporaries, and
        // begin() and end() would come from different containers.
        const QStringList names = pulled.document.clipNames();
        QCOMPARE(QSet<QString>(names.begin(), names.end()),
                 (QSet<QString>{QStringLiteral("idle"), QStringLiteral("spawn"),
                                QStringLiteral("boom"), QStringLiteral("launch"),
                                QStringLiteral("land"), QStringLiteral("bye")}));
    }

    void anUnknownClipIsRefusedRatherThanInvented()
    {
        // A new key in the catalog produces art nothing draws, and the author
        // only finds out when the companion fails to appear.
        Bridge::Result pulled =
            Bridge::importSpecies(catalog(), QStringLiteral("critter"),
                                  QStringLiteral("0"));
        QVERIFY(pulled.document.addClip(QStringLiteral("dancing")));
        const Bridge::Result pushed =
            Bridge::exportInto(catalog(), pulled.document, QStringLiteral("critter"),
                               QStringLiteral("0"));
        QCOMPARE(pushed.skipped, QStringList{QStringLiteral("dancing")});
        const QJsonObject states = pushed.catalog.value(QStringLiteral("species"))
                                       .toObject()
                                       .value(QStringLiteral("critter"))
                                       .toObject()
                                       .value(QStringLiteral("fat"))
                                       .toObject()
                                       .value(QStringLiteral("0"))
                                       .toObject();
        QVERIFY(!states.contains(QStringLiteral("dancing")));
    }

    void exportWithoutAnyKnownSequenceDoesNotProduceACatalog()
    {
        Document doc = Document::blank(1, 1);
        QVERIFY(doc.renameClip(QStringLiteral("idle"), QStringLiteral("dancing")));
        const Bridge::Result pushed =
            Bridge::exportInto(catalog(), doc, QStringLiteral("critter"),
                               QStringLiteral("0"));
        QVERIFY(!pushed.ok);
        QVERIFY(pushed.catalog.isEmpty());
    }

    void aMissingSpeciesSaysWhatThereIs()
    {
        const Bridge::Result pulled =
            Bridge::importSpecies(catalog(), QStringLiteral("cat"),
                                  QStringLiteral("0"));
        QVERIFY(!pulled.ok);
        QVERIFY(pulled.error.contains(QLatin1String("critter")));
    }

    // ----------------------------------------------------------------- Theme

    void theThemeReadsOmarchysColours()
    {
        // Pointed at a scratch XDG_STATE_HOME so the test never depends on --
        // or disturbs -- the desktop it runs on.
        QTemporaryDir state;
        QVERIFY(state.isValid());
        writeTheme(state.path(), QStringLiteral("dusk"), QStringLiteral("dark"),
                   QStringLiteral("#0c0b0c"), QStringLiteral("#b59790"),
                   QStringLiteral("#fafcfb"));
        pointAt(state.path(), QStringLiteral("dusk"));
        qputenv("XDG_STATE_HOME", state.path().toUtf8());

        Theme theme;
        QCOMPARE(theme.name(), QStringLiteral("dusk"));
        QCOMPARE(theme.background().name().toLower(), QStringLiteral("#0c0b0c"));
        QCOMPARE(theme.accent().name().toLower(), QStringLiteral("#b59790"));
        QVERIFY(theme.dark());
    }

    void theThemeFollowsASwapWhileTheWindowIsOpen()
    {
        // `omarchy theme set` repoints a symlink. The window has to recolour
        // without a restart, or the studio is the one thing on the desktop
        // still wearing the old theme.
        QTemporaryDir state;
        QVERIFY(state.isValid());
        writeTheme(state.path(), QStringLiteral("dusk"), QStringLiteral("dark"),
                   QStringLiteral("#0c0b0c"), QStringLiteral("#b59790"),
                   QStringLiteral("#fafcfb"));
        writeTheme(state.path(), QStringLiteral("noon"), QStringLiteral("light"),
                   QStringLiteral("#faf4ed"), QStringLiteral("#286983"),
                   QStringLiteral("#1f1d2e"));
        pointAt(state.path(), QStringLiteral("dusk"));
        qputenv("XDG_STATE_HOME", state.path().toUtf8());

        Theme theme;
        QCOMPARE(theme.accent().name().toLower(), QStringLiteral("#b59790"));

        QSignalSpy spy(&theme, &Theme::changed);
        pointAt(state.path(), QStringLiteral("noon"));
        QVERIFY(spy.wait(4000));
        QCOMPARE(theme.accent().name().toLower(), QStringLiteral("#286983"));
        QVERIFY(!theme.dark());
    }

    void aLightThemeDarkensWhereADarkOneLightens()
    {
        // The derived surfaces are computed rather than read, so a theme that
        // only defines the basics still produces a coherent window. Getting the
        // direction wrong gives a light theme black panels.
        QTemporaryDir state;
        writeTheme(state.path(), QStringLiteral("noon"), QStringLiteral("light"),
                   QStringLiteral("#faf4ed"), QStringLiteral("#286983"),
                   QStringLiteral("#1f1d2e"));
        pointAt(state.path(), QStringLiteral("noon"));
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        Theme light;
        QVERIFY(light.panel().lightnessF() < light.background().lightnessF());

        QTemporaryDir other;
        writeTheme(other.path(), QStringLiteral("dusk"), QStringLiteral("dark"),
                   QStringLiteral("#0c0b0c"), QStringLiteral("#b59790"),
                   QStringLiteral("#fafcfb"));
        pointAt(other.path(), QStringLiteral("dusk"));
        qputenv("XDG_STATE_HOME", other.path().toUtf8());
        Theme dark;
        QVERIFY(dark.panel().lightnessF() > dark.background().lightnessF());
    }

    void theChequerStepsAwayFromTheBackgroundEitherWay()
    {
        // `checkerLight` is the square nearer the background under BOTH modes.
        // Get it backwards and a light theme's transparency reads as a hole.
        QTemporaryDir state;
        writeTheme(state.path(), QStringLiteral("noon"), QStringLiteral("light"),
                   QStringLiteral("#eff1f5"), QStringLiteral("#1e66f5"),
                   QStringLiteral("#4c4f69"));
        pointAt(state.path(), QStringLiteral("noon"));
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        Theme light;
        QVERIFY(light.checkerLight().lightnessF() > light.checkerDark().lightnessF());
        // ... and both sit darker than the page they are drawn on.
        QVERIFY(light.checkerLight().lightnessF() < light.background().lightnessF());

        QTemporaryDir other;
        writeTheme(other.path(), QStringLiteral("dusk"), QStringLiteral("dark"),
                   QStringLiteral("#0c0b0c"), QStringLiteral("#b59790"),
                   QStringLiteral("#fafcfb"));
        pointAt(other.path(), QStringLiteral("dusk"));
        qputenv("XDG_STATE_HOME", other.path().toUtf8());
        Theme dark;
        QVERIFY(dark.checkerLight().lightnessF() > dark.checkerDark().lightnessF());
        QVERIFY(dark.checkerDark().lightnessF() > dark.background().lightnessF());
    }

    void resizingToTheSameSizeRepairsAnOffSizeFrame()
    {
        // A hand-written file can declare 8x4 and hold a frame of 12x3.
        // `resize` is what somebody reaches for to fix it, and it used to
        // return early whenever the declared size already matched -- leaving
        // `check` reporting a problem with no way to clear it.
        Document doc = Document::blank(8, 4);
        Clip *clip = doc.clip(doc.clipNames().value(0));
        clip->frames[0] = Grid::fromRows({QStringLiteral("RRRRRRRRRRRR"),
                                          QStringLiteral("R.R"),
                                          QStringLiteral("RR")});
        QVERIFY(!doc.problems().isEmpty());

        doc.resize(8, 4);
        QVERIFY(doc.problems().isEmpty());
        QCOMPARE(doc.frame(doc.clipNames().value(0), 0).columns(), 8);
        QCOMPARE(doc.frame(doc.clipNames().value(0), 0).rows(), 4);
    }

    void theLastClipCannotBeRemoved()
    {
        // A document with no clips cannot be drawn, selected from, or reopened.
        // The guard belongs here rather than in a front end: it used to live in
        // the studio alone, and the command line could write a file that the
        // studio then refused to edit.
        Document doc = Document::blank(8, 8);
        QVERIFY(doc.addClip(QStringLiteral("walk")));
        QVERIFY(doc.removeClip(QStringLiteral("walk")));
        QCOMPARE(doc.clips().size(), 1);
        QVERIFY(!doc.removeClip(doc.clipNames().value(0)));
        QCOMPARE(doc.clips().size(), 1);
    }

    // ------------------------------------------------------- the command line

    /// Runs one command line against a document, the way `batch` does.
    static cli::Outcome run(Document &doc, const QString &line)
    {
        QStringList words = QProcess::splitCommand(line);
        const QString command = words.takeFirst();
        QCommandLineParser parser;
        cli::addOptions(parser);
        parser.parse(QStringList{QStringLiteral("omapixel")} + words);
        return cli::applyCommand(doc, command, parser.positionalArguments(), parser);
    }

    void oneCommandAndABatchedOneAreTheSameCode()
    {
        // The whole point of Commands.cpp: `batch` reaches the same function a
        // single invocation does, so a behaviour cannot be right in one and
        // wrong in the other.
        Document doc = Document::blank(8, 8);
        const cli::Outcome painted = run(doc, QStringLiteral("paint --at 2,3 --slot R"));
        QCOMPARE(painted.code, 0);
        QVERIFY(painted.changed);
        QCOMPARE(doc.frame(doc.clipNames().value(0), 0).at(2, 3), QChar(u'R'));
    }

    void aCommandThatReadsDoesNotMarkTheDocumentChanged()
    {
        // `batch` saves only when something actually changed. If `info` or
        // `check` counted as an edit, every script would rewrite the file.
        Document doc = Document::blank(8, 8);
        const cli::Outcome info = run(doc, QStringLiteral("info"));
        QCOMPARE(info.code, 0);
        QVERIFY(!info.changed);
        QJsonParseError parse;
        const QJsonDocument json =
            QJsonDocument::fromJson(info.output.toUtf8(), &parse);
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QVERIFY(json.isObject());
    }

    void infoEscapesNamesAndDiffIncludesMetadata()
    {
        Document left = sample();
        QVERIFY(left.renameClip(QStringLiteral("idle"),
                                QStringLiteral("bad\"name\\line")));
        const cli::Outcome info = run(left, QStringLiteral("info"));
        QJsonParseError parse;
        const QJsonDocument json =
            QJsonDocument::fromJson(info.output.toUtf8(), &parse);
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QCOMPARE(json.object().value(QStringLiteral("clips")).toArray().first()
                     .toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("bad\"name\\line"));

        Document right = left;
        QVERIFY(right.setFps(QStringLiteral("bad\"name\\line"), 12));
        QVERIFY(!documentDifferences(left, right).isEmpty());
        right = left;
        QVERIFY(right.addClip(QStringLiteral("extra")));
        QVERIFY(!documentDifferences(left, right).isEmpty());
    }

    void diffOutputIsPinnedByteForByte()
    {
        // `omapixel diff` prints these sentences, and an agent reads them:
        // published text is an interface. Every template the comparison can
        // emit is pinned verbatim, so moving it between namespaces cannot
        // quietly reword what a script depends on.
        //
        // The lines come from the catalogue now (`diff.*` in i18n/en.json),
        // and the English floor is what the CLI has always printed, so the
        // pin holds under every language.
        Strings::shared().load(QStringLiteral("en"));
        const QString before = QStringLiteral("before");
        const QString after = QStringLiteral("after");

        // Size, a recoloured slot, a speed, and resized frames -- one of each
        // sentence the structural walk produces, in its own order.
        Document sized = Document::blank(4, 3);
        Document grown = sized;
        grown.resize(6, 5);
        grown.palette().set(u'I', QColor(QStringLiteral("#999999")));
        QVERIFY(grown.setFps(QStringLiteral("idle"), 12));
        QCOMPARE(documentDifferences(sized, grown, before, after),
                 (QStringList{
                     QStringLiteral("size: before is 4x3, after is 6x5"),
                     QStringLiteral(
                         "palette[0] I: colour is #1A1B26 in before, #999999 in after"),
                     QStringLiteral("clip[0] (idle/idle): FPS is 8 in before, 12 in after"),
                     QStringLiteral("clip[0] (idle/idle) frame 0: dimensions are "
                                    "4x3 in before, 6x5 in after")}));

        // Slot counts, slot order, slots on one side only, clip counts, and
        // clips on one side only.
        Document sparse = Document::empty(4, 3);
        sparse.palette() = Palette();
        sparse.palette().set(u'P', QColor(QStringLiteral("#010101")));
        sparse.palette().set(u'Q', QColor(QStringLiteral("#020202")));
        QVERIFY(sparse.addClip(QStringLiteral("n1")));
        Document dense = Document::empty(4, 3);
        dense.palette() = Palette();
        dense.palette().set(u'P', QColor(QStringLiteral("#010101")));
        dense.palette().set(u'R', QColor(QStringLiteral("#030303")));
        dense.palette().set(u'S', QColor(QStringLiteral("#040404")));
        QVERIFY(dense.addClip(QStringLiteral("n1")));
        QVERIFY(dense.addClip(QStringLiteral("n2")));
        QCOMPARE(documentDifferences(sparse, dense, before, after),
                 (QStringList{
                     QStringLiteral("palette: before has 2 slot(s), after has 3"),
                     QStringLiteral(
                         "palette[1]: slot is Q in before, R in after (order differs)"),
                     QStringLiteral("palette[2]: only in after (slot S)"),
                     QStringLiteral("clips: before has 1, after has 2 (count differs)"),
                     QStringLiteral("clips[1]: only in after (n2)")}));

        // A slot only on the left, and two clips whose names swapped places.
        Document loneSlot = Document::empty(4, 3);
        loneSlot.palette() = Palette();
        loneSlot.palette().set(u'Z', QColor(QStringLiteral("#050505")));
        QVERIFY(loneSlot.addClip(QStringLiteral("m")));
        QVERIFY(loneSlot.addClip(QStringLiteral("n")));
        Document swapped = Document::empty(4, 3);
        swapped.palette() = Palette();
        QVERIFY(swapped.addClip(QStringLiteral("n")));
        QVERIFY(swapped.addClip(QStringLiteral("m")));
        QCOMPARE(documentDifferences(loneSlot, swapped, before, after),
                 (QStringList{
                     QStringLiteral("palette: before has 1 slot(s), after has 0"),
                     QStringLiteral("palette[0]: only in before (slot Z)"),
                     QStringLiteral(
                         "clips[0]: name is m in before, n in after (order differs)"),
                     QStringLiteral(
                         "clips[1]: name is n in before, m in after (order differs)")}));

        // Drawn pixels, letter by letter, inside frames of one size.
        Document drawn = sample();
        Document touched = drawn;
        QVERIFY(touched.setFrame(QStringLiteral("idle"), 0,
                                 Grid::fromRows({QStringLiteral("...."),
                                                 QStringLiteral("IIII"),
                                                 QStringLiteral("....")})));
        QCOMPARE(documentDifferences(drawn, touched, before, after),
                 (QStringList{
                     QStringLiteral("clip[0] (idle/idle) frame 0: 4 pixel(s) differ")}));

        // And the empty answer, which is what `diff` exits zero on.
        QVERIFY(documentDifferences(drawn, drawn, before, after).isEmpty());
    }

    void aWrongCommandIsTwoAndARefusalIsOne()
    {
        // The split matters for scripting: 2 means fix your command, 1 means
        // fix your art.
        Document doc = Document::blank(8, 8);
        QCOMPARE(run(doc, QStringLiteral("paint --slot R")).code, 2);       // no --at
        QCOMPARE(run(doc, QStringLiteral("wibble")).code, 2);               // no such command
        QCOMPARE(run(doc, QStringLiteral("clip rm idle")).code, 1);         // the last clip
    }

    void aBatchAppliesEveryLineToTheOneDocument()
    {
        Document doc = Document::blank(8, 8);
        const QStringList script{
            QStringLiteral("palette set --slot Z --colour \"#123456\""),
            QStringLiteral("clip add walk --fps 12"),
            QStringLiteral("frame dup --clip walk"),
            QStringLiteral("rect --clip walk --frame 1 --from 1,1 --to 5,5 --slot Z --filled"),
        };
        int changes = 0;
        for (const QString &line : script) {
            const cli::Outcome outcome = run(doc, line);
            QCOMPARE(outcome.code, 0);
            if (outcome.changed)
                changes += 1;
        }
        QCOMPARE(changes, 4);
        QCOMPARE(doc.palette().colour(u'Z'), QColor(QStringLiteral("#123456")));
        QCOMPARE(doc.clip(QStringLiteral("walk"))->fps, 12);
        QCOMPARE(doc.clip(QStringLiteral("walk"))->frames.size(), 2);
        QCOMPARE(doc.frame(QStringLiteral("walk"), 1).drawnCount(), 25);
        // ... and the other clip's frame is untouched.
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).drawnCount(), 0);
    }

    void onlyDocumentCommandsCanBeBatched()
    {
        // A batch works on the one document it opened. Anything that names a
        // second file has to stay outside, or a script could quietly overwrite
        // something the person running it never mentioned.
        QVERIFY(cli::isDocumentCommand(QStringLiteral("paint")));
        QVERIFY(cli::isDocumentCommand(QStringLiteral("palette")));
        QVERIFY(cli::isDocumentCommand(QStringLiteral("check")));
        QVERIFY(cli::isDocumentCommand(QStringLiteral("trim")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("render")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("export")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("import")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("diff")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("new")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("batch")));
    }

    void trimCommandUsesTheNamedFrameAndProtectsTheOthers()
    {
        Document doc = Document::blank(8, 6);
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("........"),
                                     QStringLiteral("..I.I..."),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........"),
                                     QStringLiteral("........")}));
        doc.addFrame(QStringLiteral("idle"), 0, false);
        Grid other(8, 6);
        other.set(7, 5, u'I');
        doc.setFrame(QStringLiteral("idle"), 1, other);

        const cli::Outcome refused = run(doc, QStringLiteral("trim --frame 0"));
        QCOMPARE(refused.code, 1);
        QVERIFY(!refused.changed);
        QVERIFY(refused.error.contains(QStringLiteral("1 drawn pixel")));
        QCOMPARE(doc.columns(), 8);

        const cli::Outcome trimmed = run(doc, QStringLiteral("trim --frame 0 --anyway"));
        QCOMPARE(trimmed.code, 0);
        QVERIFY(trimmed.changed);
        QCOMPARE(doc.columns(), 3);
        QCOMPARE(doc.rows(), 1);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).toRows(),
                 (QStringList{QStringLiteral("I.I")}));
        QCOMPARE(doc.frame(QStringLiteral("idle"), 1).drawnCount(), 0);
    }

    void trimCommandRefusesEmptyAndDoesNotRewriteAlreadyTightArt()
    {
        Document empty = Document::blank(4, 3);
        const cli::Outcome refused = run(empty, QStringLiteral("trim"));
        QCOMPARE(refused.code, 1);
        QVERIFY(!refused.changed);

        Document tight = Document::blank(4, 3);
        Grid grid(4, 3);
        grid.set(0, 0, u'I');
        grid.set(3, 2, u'I');
        tight.setFrame(QStringLiteral("idle"), 0, grid);
        const cli::Outcome unchanged = run(tight, QStringLiteral("trim"));
        QCOMPARE(unchanged.code, 0);
        QVERIFY(!unchanged.changed);
        QCOMPARE(unchanged.output, QStringLiteral("already tight\n"));
    }

    // ----------------------------------------------------------- undo/redo

    void undoGoesBackAndRedoGoesForward()
    {
        DocumentModel doc;
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.canRedo());

        doc.paint(2, 3, QStringLiteral("R"));
        QCOMPARE(doc.slotAt(2, 3), QStringLiteral("R"));
        QVERIFY(doc.canUndo());

        doc.undo();
        QCOMPARE(doc.slotAt(2, 3), QStringLiteral("."));
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.canRedo());

        doc.redo();
        QCOMPARE(doc.slotAt(2, 3), QStringLiteral("R"));
        QVERIFY(doc.canUndo());
        QVERIFY(!doc.canRedo());
    }

    void aStrokeIsOneStepNoMatterHowManyPixels()
    {
        // Dragging across forty pixels and having to press ctrl-Z forty times
        // is the same as having no undo at all.
        DocumentModel doc;
        doc.beginStroke();
        for (int x = 0; x < 20; ++x)
            doc.paint(x, 5, QStringLiteral("R"));
        doc.endStroke();

        doc.undo();
        for (int x = 0; x < 20; ++x)
            QCOMPARE(doc.slotAt(x, 5), QStringLiteral("."));
        QVERIFY(!doc.canUndo());
    }

    void aStrokeThatDrawsNothingCostsNothing()
    {
        // A click that missed, or a bucket poured on its own colour. Filing a
        // step for it means undo starts with entries that do nothing, and the
        // first ctrl-Z appears to be ignored.
        DocumentModel doc;
        doc.beginStroke();
        doc.paint(2, 3, QStringLiteral("."));   // already empty
        doc.endStroke();
        QVERIFY(!doc.canUndo());
    }

    void editingAfterUndoingDropsTheRedoBranch()
    {
        DocumentModel doc;
        doc.paint(1, 1, QStringLiteral("R"));
        doc.paint(2, 2, QStringLiteral("R"));
        doc.undo();
        QVERIFY(doc.canRedo());

        doc.paint(3, 3, QStringLiteral("R"));
        QVERIFY(!doc.canRedo());
        QCOMPARE(doc.slotAt(1, 1), QStringLiteral("R"));
        QCOMPARE(doc.slotAt(2, 2), QStringLiteral("."));
        QCOMPARE(doc.slotAt(3, 3), QStringLiteral("R"));
    }

    void structuralChangesUndoToo()
    {
        // Undo that covers pixels but not "I deleted the wrong clip" is undo
        // for the cheap mistakes and not the expensive ones.
        DocumentModel doc;
        doc.addClip(QStringLiteral("walk"));
        QCOMPARE(doc.clipNames().size(), 2);
        doc.undo();
        QCOMPARE(doc.clipNames().size(), 1);

        doc.resize(64, 64);
        QCOMPARE(doc.columns(), 64);
        doc.undo();
        QCOMPARE(doc.columns(), 32);

        doc.setPaletteColour(QStringLiteral("R"), QStringLiteral("#010203"));
        doc.undo();
        QVERIFY(doc.palette().first().toMap().value(QStringLiteral("colour")).toString()
                != QStringLiteral("#010203"));
    }

    void trimmingIsOneUndoStepAndClearsTheSelection()
    {
        DocumentModel doc;
        doc.paint(10, 5, QStringLiteral("R"));
        doc.paint(12, 7, QStringLiteral("R"));
        doc.setSelection(9, 4, 13, 8);

        const QVariantMap preview = doc.trimPreview();
        QCOMPARE(preview.value(QStringLiteral("empty")).toBool(), false);
        QCOMPARE(preview.value(QStringLiteral("x")).toInt(), 10);
        QCOMPARE(preview.value(QStringLiteral("y")).toInt(), 5);
        QCOMPARE(preview.value(QStringLiteral("columns")).toInt(), 3);
        QCOMPARE(preview.value(QStringLiteral("rows")).toInt(), 3);
        QCOMPARE(preview.value(QStringLiteral("lost")).toInt(), 0);

        QVERIFY(doc.trim());
        QCOMPARE(doc.columns(), 3);
        QCOMPARE(doc.rows(), 3);
        QVERIFY(!doc.hasSelection());
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));
        QCOMPARE(doc.slotAt(2, 2), QStringLiteral("R"));

        doc.undo();
        QCOMPARE(doc.columns(), 32);
        QCOMPARE(doc.rows(), 24);
        QCOMPARE(doc.slotAt(10, 5), QStringLiteral("R"));
        doc.undo();
        QCOMPARE(doc.slotAt(12, 7), QStringLiteral("."));
    }

    void studioTrimRequiresConfirmationBeforeCroppingAnotherFrame()
    {
        DocumentModel doc;
        doc.paint(2, 2, QStringLiteral("R"));
        doc.addFrame(false);
        doc.paint(20, 20, QStringLiteral("R"));
        doc.setFrame(0);

        QCOMPARE(doc.trimPreview().value(QStringLiteral("lost")).toInt(), 1);
        QVERIFY(!doc.trim());
        QCOMPARE(doc.columns(), 32);
        QVERIFY(doc.trim(true));
        QCOMPARE(doc.columns(), 1);
        QCOMPARE(doc.rows(), 1);
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));
    }

    void undoReseatsAClipItJustRemoved()
    {
        // Undoing back past the clip you are looking at must not leave the
        // view pointing at a clip that no longer exists.
        DocumentModel doc;
        doc.addClip(QStringLiteral("walk"));
        QCOMPARE(doc.clip(), QStringLiteral("walk"));
        doc.undo();
        QCOMPARE(doc.clip(), QStringLiteral("idle"));
        QCOMPARE(doc.frame(), 0);
    }

    void aRefusedChangeFilesNoStep()
    {
        // Removing the last clip is refused. If the refusal still filed a
        // snapshot, ctrl-Z would undo something that never happened.
        DocumentModel doc;
        doc.removeClip(doc.clipNames().value(0));
        QVERIFY(!doc.canUndo());
    }

    void theHistoryIsBounded()
    {
        DocumentModel doc;
        for (int i = 0; i < 200; ++i)
            doc.paint(i % 32, i % 24, QStringLiteral("R"));
        int steps = 0;
        while (doc.canUndo() && steps < 500) {
            doc.undo();
            steps += 1;
        }
        QCOMPARE(steps, 80);
    }

    // -------------------------------------------------- following the file

    void theStudioFollowsAWriteFromTheCommandLine()
    {
        // The product thesis: an agent edits through the CLI and the change
        // appears in the window with no user action. The rest of this section
        // is guards around that sentence.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));

        Document edited = sample();
        QVERIFY(edited.setFrame(QStringLiteral("idle"), 0,
                                Grid::fromRows({QStringLiteral("IIII"),
                                                QStringLiteral("IIII"),
                                                QStringLiteral("IIII")})));
        QVERIFY(writeDocument(path, edited));

        QVERIFY(doc.reloadFromDisk());
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("I"));   // the cross was empty there
        // Memory equals disk now: not dirty. And the replaced version is
        // recoverable, which is why adopting filed a step.
        QVERIFY(!doc.dirty());
        QVERIFY(doc.canUndo());
    }

    void aReloadKeepsTheViewInsideTheNewDocument()
    {
        // The CLI can delete the clip you are looking at, or shorten it below
        // your frame. The view must land somewhere legal -- `reseat()`'s
        // policy, not a second one invented for reloads.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        Document onDisk = Document::blank(4, 3);
        QVERIFY(onDisk.addClip(QStringLiteral("walk")));
        QVERIFY(onDisk.addFrame(QStringLiteral("walk"), 0, true));   // walk: two frames
        QVERIFY(writeDocument(path, onDisk));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        doc.setClip(QStringLiteral("walk"));
        doc.setFrame(1);
        QCOMPARE(doc.frame(), 1);

        // The CLI deletes `walk`; the view falls back to the first clip and
        // keeps the frame index while it still exists there.
        Document withoutWalk = Document::blank(4, 3);
        QVERIFY(withoutWalk.addFrame(QStringLiteral("idle"), 0, true));
        QVERIFY(writeDocument(path, withoutWalk));
        QVERIFY(doc.reloadFromDisk());
        QCOMPARE(doc.clip(), QStringLiteral("idle"));
        QCOMPARE(doc.frame(), 1);

        // Then it shortens the clip past the open frame; the frame clamps.
        Document shortened = Document::blank(4, 3);
        QVERIFY(writeDocument(path, shortened));
        QVERIFY(doc.reloadFromDisk());
        QCOMPARE(doc.clip(), QStringLiteral("idle"));
        QCOMPARE(doc.frame(), 0);
    }

    void ctrlZAfterAnExternalEditRestoresYourVersion()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        doc.paint(0, 0, QStringLiteral("R"));
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));

        Document edited = sample();
        QVERIFY(edited.setFrame(QStringLiteral("idle"), 0,
                                Grid::fromRows({QStringLiteral(".I.."),
                                                QStringLiteral(".I.."),
                                                QStringLiteral(".I..")})));
        QVERIFY(writeDocument(path, edited));
        QVERIFY(doc.reloadFromDisk());
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("."));

        // Your stroke is not gone; it is one step back.
        doc.undo();
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));
        // ... and reaching for it is what makes the document dirty again.
        QVERIFY(doc.dirty());
        QVERIFY(doc.canRedo());
    }

    void reloadingEqualContentFilesNoStep()
    {
        // Saving from the studio fires the watcher with bytes we just wrote.
        // If that landed an undo entry, every Ctrl+S would eat one step of
        // history -- and arm the dirty bullet over an identical file.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        QVERIFY(doc.save());
        QVERIFY(!doc.canUndo());

        // A second CLI write of identical content is also nothing: a touch or
        // a reformat may not become somebody's undo step.
        QVERIFY(writeDocument(path, sample()));
        QVERIFY(!doc.reloadFromDisk());
        QVERIFY(!doc.canUndo());
        QVERIFY(!doc.dirty());
    }

    void aReloadLandingMidStrokeWaitsForTheStrokeToEnd()
    {
        // A stroke holds state across press-to-release. Applying a reload
        // inside that window swaps the document under the pen and the rest of
        // the drag paints onto the file's version with a stale remembered
        // flag. So it waits -- then lands as exactly one more step.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));

        doc.beginStroke();
        doc.paint(3, 2, QStringLiteral("R"));
        QCOMPARE(doc.slotAt(3, 2), QStringLiteral("R"));

        Document edited = Document::blank(4, 3);
        QVERIFY(writeDocument(path, edited));
        QVERIFY(!doc.reloadFromDisk());            // queued, not applied
        QCOMPARE(doc.slotAt(3, 2), QStringLiteral("R"));

        doc.endStroke();                           // now it applies
        QCOMPARE(doc.slotAt(3, 2), QStringLiteral("."));
        QVERIFY(!doc.canRedo());

        // One undo reaches YOUR last stroke, not the file's version again...
        doc.undo();
        QCOMPARE(doc.slotAt(3, 2), QStringLiteral("R"));
        // ... and one more reaches the document as it was opened.
        doc.undo();
        QCOMPARE(doc.slotAt(0, 1), QStringLiteral("I"));
    }

    void aBrokenOrMissingFileLeavesTheCanvasAlone()
    {
        // The directory watch hears about neighbours too: an export elsewhere,
        // an rm, an editor's swapfile, a half-written file. None of those may
        // blank what is on screen; they get said instead.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        doc.paint(1, 1, QStringLiteral("R"));

        QFile broken(path);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ not a document");
        broken.close();
        QVERIFY(!doc.reloadFromDisk());
        QCOMPARE(doc.slotAt(1, 1), QStringLiteral("R"));
        QVERIFY(!doc.note().isEmpty());

        QVERIFY(QFile::remove(path));
        QVERIFY(!doc.reloadFromDisk());
        QCOMPARE(doc.slotAt(1, 1), QStringLiteral("R"));
        QVERIFY(!doc.note().isEmpty());
    }

    void theStudioPicksUpACommandLineWriteOnItsOwn()
    {
        // The wiring itself: nothing drives the model here, the write happens
        // behind its back, and adoption arrives through the watcher. One
        // atomic rename fires BOTH watched paths (the file's inode changed
        // and the directory's contents did); content equality collapses them,
        // so exactly ONE undo entry may exist afterwards.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));

        Document edited = sample();
        QVERIFY(edited.setFrame(QStringLiteral("idle"), 0,
                                Grid::fromRows({QStringLiteral("IIII"),
                                                QStringLiteral(".II."),
                                                QStringLiteral(".II.")})));
        QSignalSpy spy(&doc, &DocumentModel::changed);
        QVERIFY(writeDocument(path, edited));
        QVERIFY(spy.wait(4000));

        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("I"));
        // Let any second delivery land before counting. It cannot raise the
        // count: the second fire finds the documents equal and emits
        // nothing -- which is precisely the collapse being proven.
        QTest::qWait(250);
        QCOMPARE(spy.count(), 1);

        // Exactly one step between what is on screen and what was opened.
        doc.undo();
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("."));
        QVERIFY(!doc.canUndo());
    }

    void aReloadSaysWhatChangedOnTheStatusBar()
    {
        // A document that changes under your hands with no signal is a trick.
        // The note names the file and says WHAT changed, from the same core
        // walk `omapixel diff` prints.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));

        Document edited = sample();
        QVERIFY(edited.setFrame(QStringLiteral("idle"), 0,
                                Grid::fromRows({QStringLiteral("IIII"),
                                                QStringLiteral("IIII"),
                                                QStringLiteral("IIII")})));
        QVERIFY(writeDocument(path, edited));
        QVERIFY(doc.reloadFromDisk());

        QCOMPARE(doc.note(),
                 QStringLiteral("drawing.json changed on disk: ")
                     + QStringLiteral(
                         "clip[0] (idle/idle) frame 0: 4 pixel(s) differ"));
    }

    void aReloadThatReplacedUnsavedWorkSaysSo()
    {
        // The data-loss shape: agent writes over unsaved strokes. The note
        // has to say what changed AND that Ctrl+Z brings yours back -- the
        // next Ctrl+S would otherwise destroy the agent's edit with nobody
        // the wiser.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        doc.paint(0, 0, QStringLiteral("R"));

        QVERIFY(writeDocument(path, Document::blank(4, 3)));
        QVERIFY(doc.reloadFromDisk());
        QCOMPARE(doc.note(),
                 QStringLiteral("drawing.json changed on disk: ")
                     + QStringLiteral("clip[0] (idle/idle) frame 0: 9 pixel(s) differ")
                     + QStringLiteral(
                         " — it replaced unsaved work, Ctrl+Z brings yours back"));

        // ... and the promise holds.
        doc.undo();
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));
    }

    void anAgentLoopFilesOneUndoEntryNotEighty()
    {
        // `batch` or a looping agent can rewrite the file many times in a
        // second. Every snapshot is a whole document and the stack is capped,
        // so filing one per write evicts the user's own history within
        // seconds. While nothing of theirs sits on top of an adopted state,
        // ONE entry keeps standing for "your version".
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));

        for (int i = 0; i < 100; ++i) {
            const int x = i % 4;
            const int y = i % 3;
            QStringList rows{QStringLiteral("...."), QStringLiteral("...."),
                             QStringLiteral("....")};
            rows[y].replace(x, 1, QStringLiteral("R"));
            Document edited = Document::blank(4, 3);
            QVERIFY(edited.setFrame(QStringLiteral("idle"), 0,
                                    Grid::fromRows(rows)));
            QVERIFY(writeDocument(path, edited));
            QVERIFY(doc.reloadFromDisk());
        }

        // The hundredth write moved the dot to (3, 0); it is on screen.
        QCOMPARE(doc.slotAt(3, 0), QStringLiteral("R"));

        int steps = 0;
        while (doc.canUndo() && steps < 500) {
            doc.undo();
            steps += 1;
        }
        QCOMPARE(steps, 1);
        QCOMPARE(doc.slotAt(0, 1), QStringLiteral("I"));   // back to what was opened
    }

    // ------------------------------------------------------------- sessions

    void selectionIsViewStateAndNotADocumentEdit()
    {
        DocumentModel doc;
        QSignalSpy changed(&doc, &DocumentModel::selectionChanged);

        doc.setSelection(7, 6, 2, 3);
        QVERIFY(doc.hasSelection());
        QCOMPARE(doc.selectionX(), 2);
        QCOMPARE(doc.selectionY(), 3);
        QCOMPARE(doc.selectionWidth(), 6);
        QCOMPARE(doc.selectionHeight(), 4);
        QCOMPARE(doc.selectionCount(), 24);
        QCOMPARE(changed.size(), 1);
        QVERIFY(!doc.dirty());
        QVERIFY(!doc.canUndo());

        doc.setFrame(0); // no view change, so the rectangle remains
        QVERIFY(doc.hasSelection());
        doc.clearSelection();
        QVERIFY(!doc.hasSelection());
        QCOMPARE(changed.size(), 2);

        doc.addFrame(false);
        doc.setSelection(0, 0, 2, 2);
        doc.setFrame(0);
        QVERIFY(!doc.hasSelection());
    }

    void paintingTargetsTheSelectionBeforeTheCaret()
    {
        DocumentModel doc;
        doc.setSelection(2, 3, 4, 5);

        doc.paint(20, 20, QStringLiteral("R"));
        QVERIFY(doc.hasSelection());
        for (int y = 3; y <= 5; ++y)
            for (int x = 2; x <= 4; ++x)
                QCOMPARE(doc.slotAt(x, y), QStringLiteral("R"));
        QCOMPARE(doc.slotAt(20, 20), QStringLiteral("."));

        // The rectangle is one edit no matter how many cells it covers.
        doc.undo();
        for (int y = 3; y <= 5; ++y)
            for (int x = 2; x <= 4; ++x)
                QCOMPARE(doc.slotAt(x, y), QStringLiteral("."));

        // Without a selection the same entry point falls back to one pixel.
        doc.paint(20, 20, QStringLiteral("R"));
        QCOMPARE(doc.slotAt(20, 20), QStringLiteral("R"));
        QCOMPARE(doc.slotAt(2, 3), QStringLiteral("."));
    }

    void copiedPixelsAreAColourMatrixIndependentOfPaletteSlots()
    {
        DocumentModel doc;
        doc.reset(4, 3);
        doc.setPaletteColour(QStringLiteral("Z"), QStringLiteral("#123456"));
        doc.paint(1, 0, QStringLiteral("Z"));
        doc.paint(2, 1, QStringLiteral("R"));
        doc.setSelection(1, 0, 2, 1);

        QVERIFY(doc.copySelection());
        const QJsonDocument copied = QJsonDocument::fromJson(
            QGuiApplication::clipboard()->text().toUtf8());
        QVERIFY(copied.isArray());
        const QJsonArray rows = copied.array();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toArray(),
                 QJsonArray({QStringLiteral("#123456"), QJsonValue::Null}));
        QCOMPARE(rows.at(1).toArray(),
                 QJsonArray({QJsonValue::Null, QStringLiteral("#F7768E")}));
    }

    void pastedPixelsReuseDestinationColoursAndNullErases()
    {
        DocumentModel doc;
        doc.reset(4, 3);
        doc.setPaletteColour(QStringLiteral("Q"), QStringLiteral("#123456"));
        doc.paint(2, 1, QStringLiteral("I"));
        const int paletteSize = doc.palette().size();
        QGuiApplication::clipboard()->setText(QStringLiteral(
            "[[\"#123456\",null],[\"#F7768E\",\"#123456\"]]"));

        QVERIFY(doc.pastePixels(1, 1));
        QCOMPARE(doc.slotAt(1, 1), QStringLiteral("Q"));
        QCOMPARE(doc.slotAt(2, 1), QStringLiteral("."));
        QCOMPARE(doc.slotAt(1, 2), QStringLiteral("R"));
        QCOMPARE(doc.slotAt(2, 2), QStringLiteral("Q"));
        QCOMPARE(doc.palette().size(), paletteSize);
        QCOMPARE(doc.selectionX(), 1);
        QCOMPARE(doc.selectionY(), 1);
        QCOMPARE(doc.selectionWidth(), 2);
        QCOMPARE(doc.selectionHeight(), 2);
    }

    void pasteAddsNewColoursInOneUndoStep()
    {
        DocumentModel doc;
        const int paletteSize = doc.palette().size();
        QGuiApplication::clipboard()->setText(QStringLiteral(
            "[[\"#010203\",\"#010203\"]]"));

        QVERIFY(doc.pastePixels(3, 4));
        const QString newSlot = doc.slotAt(3, 4);
        QVERIFY(newSlot != QStringLiteral("."));
        QCOMPARE(doc.slotAt(4, 4), newSlot);
        QCOMPARE(doc.colourOf(newSlot), QColor(QStringLiteral("#010203")));
        QCOMPARE(doc.palette().size(), paletteSize + 1);

        doc.undo();
        QCOMPARE(doc.slotAt(3, 4), QStringLiteral("."));
        QCOMPARE(doc.slotAt(4, 4), QStringLiteral("."));
        QCOMPARE(doc.palette().size(), paletteSize);
    }

    void pasteRejectsInvalidMatricesWithoutEditing()
    {
        const QStringList invalid{
            QStringLiteral("not json"),
            QStringLiteral("[]"),
            QStringLiteral("[[]]"),
            QStringLiteral("[[\"#FFF\"]]"),
            QStringLiteral("[[\"red\"]]"),
            QStringLiteral("[[1]]"),
            QStringLiteral("[[\"#123456\"],[null,null]]"),
            QStringLiteral("{\"pixels\":[[\"#123456\"]]}")
        };

        for (const QString &text : invalid) {
            DocumentModel doc;
            QGuiApplication::clipboard()->setText(text);
            QVERIFY2(!doc.pastePixels(0, 0), qPrintable(text));
            QCOMPARE(doc.slotAt(0, 0), QStringLiteral("."));
            QVERIFY(!doc.dirty());
            QVERIFY(!doc.canUndo());
        }
    }

    void pasteCropsAtTheCanvasEdge()
    {
        DocumentModel doc;
        doc.reset(3, 2);
        QGuiApplication::clipboard()->setText(QStringLiteral(
            "[[\"#F7768E\",\"#7AA2F7\"],[\"#9ECE6A\",null]]"));

        QVERIFY(doc.pastePixels(2, 1));
        QCOMPARE(doc.slotAt(2, 1), QStringLiteral("R"));
        QCOMPARE(doc.selectionX(), 2);
        QCOMPARE(doc.selectionY(), 1);
        QCOMPARE(doc.selectionWidth(), 1);
        QCOMPARE(doc.selectionHeight(), 1);
    }

    void pasteIsAtomicWhenThePaletteHasNoFreeSlot()
    {
        DocumentModel doc;
        QString free;
        while (!(free = doc.freeSlot()).isEmpty())
            doc.setPaletteColour(free, QStringLiteral("#010203"));
        doc.paint(0, 0, QStringLiteral("I"));
        const int paletteSize = doc.palette().size();
        QGuiApplication::clipboard()->setText(QStringLiteral(
            "[[\"#040506\",null]]"));

        QVERIFY(!doc.pastePixels(0, 0));
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("I"));
        QCOMPARE(doc.slotAt(1, 0), QStringLiteral("."));
        QCOMPARE(doc.palette().size(), paletteSize);
    }

    void aSessionSaysWhatTheStudioHoldsOpen()
    {
        // The agent-side half of the live loop: before writing, an agent can
        // ask whether a studio holds the file open and whether that studio
        // has unsaved work. Asserted against the C++ object, which is where
        // the behaviour lives -- there is no QML harness here and none needed.
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString drawing = runtime.path() + QStringLiteral("/heart.json");
        QVERIFY(writeDocument(drawing, sample()));

        DocumentModel doc;
        SessionPublisher sessions;
        sessions.follow(&doc);

        const QString sessionPath =
            sessions::directory() + QStringLiteral("/%1.json")
                .arg(QCoreApplication::applicationPid());
        const auto published = [&sessionPath] {
            // A failed open parses to an empty object, and the comparisons
            // below fail loudly enough.
            QFile file(sessionPath);
            file.open(QIODevice::ReadOnly);
            return QJsonDocument::fromJson(file.readAll()).object();
        };

        // Untitled: `path` exists and names the scratch backing -- the
        // whole point of the file is that the command line can reach a
        // window nobody has saved yet.
        QVERIFY(sessions::startTimeOf(QCoreApplication::applicationPid()) > 0);
        const QString scratch =
            sessions::scratchPath(QCoreApplication::applicationPid());
        QVERIFY(!scratch.isEmpty());
        QJsonObject session = published();
        QCOMPARE(session.value(QStringLiteral("pid")).toDouble(),
                 double(QCoreApplication::applicationPid()));
        QCOMPARE(
            session.value(QStringLiteral("started")).toDouble(),
            double(sessions::startTimeOf(QCoreApplication::applicationPid())));
        QCOMPARE(session.value(QStringLiteral("path")).toString(), scratch);
        QCOMPARE(session.value(QStringLiteral("dirty")).toBool(), false);
        QVERIFY(session.value(QStringLiteral("selection")).isNull());

        QVERIFY(doc.open(drawing));
        session = published();
        QCOMPARE(session.value(QStringLiteral("path")).toString(), drawing);
        QCOMPARE(session.value(QStringLiteral("dirty")).toBool(), false);
        // A named document has no need of its scratch backing.
        QVERIFY(!QFile::exists(scratch));

        doc.paint(1, 1, QStringLiteral("R"));
        session = published();
        QCOMPARE(session.value(QStringLiteral("path")).toString(), drawing);
        QCOMPARE(session.value(QStringLiteral("dirty")).toBool(), true);

        doc.setSelection(0, 0, 3, 2);
        session = published();
        const QJsonObject selection =
            session.value(QStringLiteral("selection")).toObject();
        QCOMPARE(selection.value(QStringLiteral("clip")).toString(),
                 QStringLiteral("idle"));
        QCOMPARE(selection.value(QStringLiteral("frame")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("x")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("y")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("width")).toInt(), 4);
        QCOMPARE(selection.value(QStringLiteral("height")).toInt(), 3);
        QCOMPARE(selection.value(QStringLiteral("count")).toInt(), 12);
        const QList<sessions::Entry> live = sessions::live(drawing);
        QCOMPARE(live.size(), 1);
        QCOMPARE(live.first().clip, QStringLiteral("idle"));
        QCOMPARE(live.first().frame, 0);
        QCOMPARE(live.first().selection, QRect(0, 0, 4, 3));

        QVERIFY(doc.save());
        session = published();
        QCOMPARE(session.value(QStringLiteral("dirty")).toBool(), false);
    }

    void retiringARemovesTheSessionFile()
    {
        // The file is a claim on "a studio is here". Left behind by a clean
        // exit it would lie; retired it says only what is true.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        SessionPublisher sessions;
        sessions.follow(&doc);
        const QString sessionPath =
            sessions::directory() + QStringLiteral("/%1.json")
                .arg(QCoreApplication::applicationPid());
        QVERIFY(QFile::exists(sessionPath));

        sessions.retire();
        QVERIFY(!QFile::exists(sessionPath));
    }

    void twoStudiosPublishBesideEachOther()
    {
        // One file per process: two windows on two documents never merge and
        // never overwrite each other.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        SessionPublisher mine;
        mine.follow(&doc);

        const QString theirsPath =
            sessions::directory() + QStringLiteral("/999999.json");
        QFile theirs(theirsPath);
        QVERIFY(theirs.open(QIODevice::WriteOnly));
        theirs.write("{\"pid\": 999999}\n");
        theirs.close();

        doc.paint(2, 2, QStringLiteral("R"));   // rewrites ours, only ours

        QFile after(theirsPath);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(after.readAll()),
                 QStringLiteral("{\"pid\": 999999}\n"));
        QFile ours(sessions::directory() + QStringLiteral("/%1.json")
                                             .arg(QCoreApplication::applicationPid()));
        QVERIFY(ours.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(ours.readAll())
                    .object()
                    .value(QStringLiteral("dirty"))
                    .toBool());
    }

    // ------------------------------------------------------ scratch backing

    void anUntitledWindowIsBackedByAScratchFile()
    {
        // The addressability contract: open the studio with nothing, and
        // there is still somewhere for `omapixel where` to point and an
        // agent to draw. The seed goes through the same atomic writer as
        // everything else.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        QCOMPARE(doc.followedPath(),
                 sessions::scratchPath(QCoreApplication::applicationPid()));
        QVERIFY(QFile::exists(doc.followedPath()));

        // Drawing does NOT mirror into the file: once seeded, the file
        // belongs to whoever writes it -- usually an agent -- and adoption
        // flows the other way.
        doc.paint(1, 1, QStringLiteral("R"));
        const Codec::Result seeded =
            Codec::readFile(doc.followedPath(), Codec::WarningLimits());
        QVERIFY(seeded);
        QVERIFY(!seeded.document.usesSlot(u'R'));
    }

    void anAgentDrawsIntoAnUntitledWindowLive()
    {
        // The flow that named the feature: a fresh studio, no file ever
        // opened, and the command line still reaches the drawing.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        QVERIFY(doc.isScratchBacked());

        Document edited = Document::blank(32, 24);
        QStringList rows;
        for (int y = 0; y < 24; ++y) {
            QString row(32, u'.');
            row[1] = u'I';
            rows << row;
        }
        QVERIFY(edited.setFrame(QStringLiteral("idle"), 0, Grid::fromRows(rows)));
        QSignalSpy spy(&doc, &DocumentModel::changed);
        QVERIFY(writeDocument(doc.followedPath(), edited));
        QVERIFY(spy.wait(4000));
        QCOMPARE(doc.slotAt(1, 0), QStringLiteral("I"));
        // The window was pristine; adopting the agent's version is not
        // unsaved work appearing from nowhere.
        QVERIFY(!doc.dirty());

        // Then the user draws, and a later adoption must not pretend their
        // stroke was saved just because the disk changed underneath it.
        doc.paint(0, 0, QStringLiteral("R"));
        QVERIFY(doc.dirty());
        QSignalSpy second(&doc, &DocumentModel::changed);
        QVERIFY(writeDocument(doc.followedPath(), Document::blank(32, 24)));
        QVERIFY(second.wait(4000));
        QVERIFY(doc.isScratchBacked());
        QVERIFY(doc.dirty());          // tmpfs is not a save
        doc.undo();
        QCOMPARE(doc.slotAt(0, 0), QStringLiteral("R"));   // theirs, one step back
    }

    void resettingAWindowReseedsItsScratch()
    {
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        doc.paint(1, 1, QStringLiteral("R"));   // screen drifts from the seed
        doc.reset(8, 8);

        // Same address, describing the NEW document rather than the old one.
        QCOMPARE(doc.followedPath(),
                 sessions::scratchPath(QCoreApplication::applicationPid()));
        const Codec::Result seeded =
            Codec::readFile(doc.followedPath(), Codec::WarningLimits());
        QVERIFY(seeded);
        QCOMPARE(seeded.document.columns(), 8);
        QCOMPARE(seeded.document.rows(), 8);
        QVERIFY(!seeded.document.usesSlot(u'R'));
    }

    void turningScratchOffKeepsUntitledWindowsInvisible()
    {
        // The off switch restores yesterday exactly: no backing file, an
        // empty advertised path, nothing for `where` to name.
        QTemporaryDir cfg;
        QFile config(cfg.path() + QStringLiteral("/config.toml"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("[studio]\nscratch = false\n");
        config.close();
        qputenv("OMAPIXEL_CONFIG_PATH", config.fileName().toUtf8());
        Config::shared().load();

        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        SessionPublisher publisher;
        publisher.follow(&doc);

        QCOMPARE(doc.followedPath(), QString());
        QVERIFY(!QFile::exists(
            sessions::scratchPath(QCoreApplication::applicationPid())));
        QFile session(sessions::directory() + QStringLiteral("/%1.json")
                                                  .arg(QCoreApplication::applicationPid()));
        QVERIFY(session.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(session.readAll())
                     .object()
                     .value(QStringLiteral("path"))
                     .toString(),
                 QString());

        // Put the suite's config back the way initTestCase found it.
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config::shared().load();
    }

    // ---------------------------------------------------------------- where

    void whereFindsTheStudioHoldingADocument()
    {
        // The read side of the session contract: `live()` reports the
        // sessions whose pid-and-start-time still name a running process.
        // This test IS that running process.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString drawing =
            QDir(runtime.path()).absoluteFilePath(QStringLiteral("heart.json"));
        QJsonObject body;
        body.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        body.insert(
            QStringLiteral("started"),
            double(sessions::startTimeOf(QCoreApplication::applicationPid())));
        body.insert(QStringLiteral("path"), drawing);
        body.insert(QStringLiteral("dirty"), false);
        body.insert(QStringLiteral("selection"), QJsonValue::Null);

        QDir().mkpath(sessions::directory());
        QFile file(sessions::directory() + QStringLiteral("/%1.json")
                                              .arg(QCoreApplication::applicationPid()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(body).toJson(QJsonDocument::Compact));
        file.close();

        const QList<sessions::Entry> found = sessions::live(drawing);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().pid, qint64(QCoreApplication::applicationPid()));
        QCOMPARE(found.first().path, drawing);
        QVERIFY(!found.first().dirty);
        QVERIFY(!found.first().selection.isValid());
    }

    void wherePrunesADeadSession()
    {
        // A session whose process is gone must not be reported -- and must
        // not sit in the directory forever, either. The child here is reaped,
        // so its /proc entry is gone and no start time can match.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        QProcess dead;
        dead.start(QStringLiteral("true"));
        QVERIFY(dead.waitForStarted());
        QVERIFY(dead.waitForFinished());

        QDir().mkpath(sessions::directory());
        QFile file(sessions::directory() + QStringLiteral("/%1.json").arg(dead.processId()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QString("{\"pid\": %1, \"started\": 123456, \"path\": "
                           "\"/tmp/x.json\", \"dirty\": false}")
                       .arg(dead.processId())
                       .toUtf8());
        file.close();

        QVERIFY(sessions::live().isEmpty());
        QVERIFY(!QFile::exists(file.fileName()));
    }

    void wherePrunesARecycledSession()
    {
        // The case a name check alone would pass: the PID is alive, but it
        // belongs to a different process than the one that wrote the file --
        // which the recorded start time exposes. This test's own PID plays
        // the impostor.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        QJsonObject body;
        body.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        body.insert(
            QStringLiteral("started"),
            double(sessions::startTimeOf(QCoreApplication::applicationPid()) + 1));
        body.insert(QStringLiteral("path"), QStringLiteral("/tmp/someone.json"));
        body.insert(QStringLiteral("dirty"), true);

        QDir().mkpath(sessions::directory());
        QFile file(sessions::directory() + QStringLiteral("/%1.json")
                                              .arg(QCoreApplication::applicationPid()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(body).toJson(QJsonDocument::Compact));
        file.close();

        QVERIFY(sessions::live().isEmpty());
        QVERIFY(!QFile::exists(file.fileName()));
    }

    void whereReportsBothStudiosOnOneDocument()
    {
        // Two windows on one document: two entries, because inventing a
        // winner would be worse than making the caller choose. The second
        // live process here is a real child, so both records validate.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        QProcess other;
        other.start(QStringLiteral("sleep"), {QStringLiteral("10")});
        QVERIFY(other.waitForStarted());
        const qint64 otherStarted = sessions::startTimeOf(other.processId());
        QVERIFY(otherStarted > 0);

        QDir().mkpath(sessions::directory());
        const QString drawing = QStringLiteral("/tmp/shared.json");
        const auto writeSession = [&](qint64 pid, qint64 started) {
            QFile f(sessions::directory() + QStringLiteral("/%1.json").arg(pid));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString("{\"pid\": %1, \"started\": %2, \"path\": \"%3\", "
                            "\"dirty\": false, \"selection\": null}")
                        .arg(pid)
                        .arg(started)
                        .arg(drawing)
                        .toUtf8());
        };
        writeSession(QCoreApplication::applicationPid(),
                     sessions::startTimeOf(QCoreApplication::applicationPid()));
        writeSession(other.processId(), otherStarted);

        QCOMPARE(sessions::live(drawing).size(), 2);

        other.kill();
        QVERIFY(other.waitForFinished());
    }

    // -------------------------------------------------------- change history

    /// Reads one role out of the log's row.
    static QVariant historyAt(const ChangeLog &log, int row, int role)
    {
        return log.index(row, 0).data(role);
    }

    void drawingFilesAStudioEntry()
    {
        // The session record: what changed, by whose hand. The user knows
        // what they drew; the entry exists so the whole session reads as one
        // story, studio hand and outside writes alike. Window zero, so every
        // change is its own entry no matter how fast the machine runs.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        ChangeLog log(0);
        log.follow(&doc);

        doc.paint(1, 1, QStringLiteral("R"));
        QCOMPARE(log.count(), 1);
        QCOMPARE(historyAt(log, 0, ChangeLog::OriginRole).toString(),
                 QStringLiteral("studio"));
        QVERIFY(historyAt(log, 0, ChangeLog::DescriptionRole).toString()
                    .contains(QStringLiteral("pixel(s) differ")));
        QVERIFY(historyAt(log, 0, ChangeLog::WhenRole).toLongLong() > 0);
    }

    void anExternalWriteFilesACliEntry()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/drawing.json");
        QVERIFY(writeDocument(path, sample()));

        DocumentModel doc;
        QVERIFY(doc.open(path));
        ChangeLog log(0);
        log.follow(&doc);

        QVERIFY(writeDocument(path, Document::blank(4, 3)));
        QVERIFY(doc.reloadFromDisk());

        QCOMPARE(log.count(), 1);
        QCOMPARE(historyAt(log, 0, ChangeLog::OriginRole).toString(),
                 QStringLiteral("cli"));
        // Described by the same core walk `omapixel diff` prints.
        QVERIFY(historyAt(log, 0, ChangeLog::DescriptionRole).toString()
                    .contains(QStringLiteral("8 pixel(s) differ")));
    }

    void aDragReadsAsOneSentenceAndUndoAsAnother()
    {
        // The window's own log coalesces: a stroke is one sentence that
        // swells, not one per pixel. And undo is itself something that
        // happened, so it lands as its own entry -- while every earlier
        // entry survives untouched, because this is a record and not a
        // mechanism.
        DocumentModel doc;
        ChangeLog *log = doc.findChild<ChangeLog *>();
        QVERIFY(log);

        doc.beginStroke();
        doc.paint(1, 1, QStringLiteral("R"));
        doc.paint(2, 2, QStringLiteral("R"));
        doc.endStroke();
        QCOMPARE(log->count(), 1);
        QVERIFY(historyAt(*log, 0, ChangeLog::OriginRole).toString()
                    == QStringLiteral("studio"));
        QVERIFY(historyAt(*log, 0, ChangeLog::DescriptionRole).toString()
                    .contains(QStringLiteral("2 pixel(s) differ")));

        // Outrun the coalescing window before undoing, so the undo cannot be
        // mistaken for more drawing.
        QTest::qWait(850);
        doc.undo();
        QCOMPARE(log->count(), 2);
        QCOMPARE(historyAt(*log, 0, ChangeLog::DescriptionRole).toString()
                     .contains(QStringLiteral("2 pixel(s) differ")),
                 true);   // the old entry still reads as it did
        QCOMPARE(historyAt(*log, 1, ChangeLog::OriginRole).toString(),
                 QStringLiteral("studio"));
    }

    // --------------------------------------------------------------- wheel

    /// Builds a throwaway view with two wheel handlers and reports which one a
    /// synthesised wheel event reaches.
    static QQuickItem *wheelProbe(QQuickView *view)
    {
        static const char *source = R"(
            import QtQuick
            Item {
                width: 200; height: 200
                property int plain: 0
                property int zoomed: 0
                property int sideways: 0
                property int anyModifiers: -1
                WheelHandler { onWheel: function (e) { parent.anyModifiers = e.modifiers } }
                WheelHandler { acceptedModifiers: Qt.NoModifier;      onWheel: parent.plain += 1 }
                WheelHandler { acceptedModifiers: Qt.ControlModifier; onWheel: parent.zoomed += 1 }
                WheelHandler { acceptedModifiers: Qt.ShiftModifier;   onWheel: parent.sideways += 1 }
            }
        )";
        auto *component = new QQmlComponent(view->engine(), view);
        component->setData(source, QUrl());
        if (!component->isReady())
            qWarning("%s", qPrintable(component->errorString()));
        view->setContent(QUrl(), component, component->create());
        view->resize(200, 200);
        view->show();
        return view->rootObject();
    }

    static void sendWheel(QQuickView *view, Qt::KeyboardModifiers modifiers)
    {
        const QPointF at(100, 100);
        QWindowSystemInterface::handleWheelEvent(view, at, view->mapToGlobal(at),
                                                 QPoint(0, 0), QPoint(0, 120),
                                                 modifiers);
        QCoreApplication::processEvents();
    }

    void aModifiedWheelReachesTheHandlerThatAskedForThatModifier()
    {
        // Written after two wrong guesses about why ctrl-wheel would not zoom.
        // `acceptedModifiers` matches the WHOLE modifier set, so a handler
        // declared `Shift | Control` fires for neither -- it wants both keys at
        // once. This pins the contract down rather than reasoning about it.
        QQuickView view;
        QQuickItem *probe = wheelProbe(&view);
        QVERIFY(probe);
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        sendWheel(&view, Qt::NoModifier);
        QCOMPARE(probe->property("plain").toInt(), 1);
        QCOMPARE(probe->property("zoomed").toInt(), 0);

        sendWheel(&view, Qt::ControlModifier);
        QCOMPARE(probe->property("zoomed").toInt(), 1);
        QCOMPARE(probe->property("plain").toInt(), 1);

        sendWheel(&view, Qt::ShiftModifier);
        QCOMPARE(probe->property("sideways").toInt(), 1);
        QCOMPARE(probe->property("zoomed").toInt(), 1);

        // And a handler that accepts anything can read the modifiers off the
        // event, which was the other half of the guesswork.
        sendWheel(&view, Qt::ControlModifier);
        QCOMPARE(probe->property("anyModifiers").toInt(), int(Qt::ControlModifier));
    }

    void aMouseAreaOnTopDoesNotSwallowTheWheel()
    {
        // The real surface has a MouseArea covering it for panning, above the
        // wheel handlers. If an Item that does not handle wheel events still
        // stopped them reaching a handler underneath, every scroll in the
        // studio would vanish -- which is exactly the symptom being chased.
        static const char *source = R"(
            import QtQuick
            Item {
                width: 200; height: 200
                property int plain: 0
                property int zoomed: 0
                WheelHandler { acceptedModifiers: Qt.NoModifier;      onWheel: parent.plain += 1 }
                WheelHandler { acceptedModifiers: Qt.ControlModifier; onWheel: parent.zoomed += 1 }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton
                    z: 5
                }
            }
        )";
        QQuickView view;
        auto *component = new QQmlComponent(view.engine(), &view);
        component->setData(source, QUrl());
        QVERIFY2(component->isReady(), qPrintable(component->errorString()));
        view.setContent(QUrl(), component, component->create());
        view.resize(200, 200);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQuickItem *probe = view.rootObject();

        sendWheel(&view, Qt::NoModifier);
        QCOMPARE(probe->property("plain").toInt(), 1);

        sendWheel(&view, Qt::ControlModifier);
        QCOMPARE(probe->property("zoomed").toInt(), 1);
    }

    void aWheelOverTheRealSurfaceScrollsAndZooms()
    {
        // Loads src/gui/qml/Surface.qml itself, not a sketch of it. Three
        // rounds of "scrolling does not work" were spent testing simplified
        // stand-ins that behaved perfectly; the only thing worth testing is the
        // file that ships.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QTest::failOnWarning();
        DocumentModel document;
        document.reset(854, 480);
        Theme theme;
        QQuickView view;
        // Surface.qml has no size of its own -- in the window it takes one from
        // the layout. Without this it loads at 0x0 and no pointer event can
        // reach it, which looks exactly like the bug being hunted.
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        // `win` is the window in Main.qml. Surface reads a handful of view
        // state off it; here it is a stand-in carrying the same properties.
        QQmlComponent stub(view.engine());
        stub.setData(R"(
            import QtQuick
            QtObject {
                property real zoom: 12
                property bool mesh: true
                property bool onion: false
                property string tool: "pencil"
                property string slot: "I"
                property string referencePath: ""
                property real referenceAlpha: 0.5
                property bool referenceOnTop: false
                property var linePoints: []
                property int caretColumn: -1
                property int caretRow: -1
            }
        )", QUrl());
        QVERIFY2(stub.isReady(), qPrintable(stub.errorString()));
        QObject *win = stub.create();
        QVERIFY(win);

        view.rootContext()->setContextProperty(QStringLiteral("win"), win);
        view.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        view.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        view.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        view.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        static InputLog quiet(false);
        view.rootContext()->setContextProperty(QStringLiteral("log"), &quiet);

        QQmlComponent surfaceComponent(
            view.engine(),
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Surface.qml")));
        QVERIFY2(surfaceComponent.isReady(), qPrintable(surfaceComponent.errorString()));
        view.setContent(QUrl(), &surfaceComponent, surfaceComponent.create());
        view.resize(1600, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QQuickItem *surface = view.rootObject();
        QVERIFY(surface);

        // Fit uses the full available pane instead of throwing away the
        // fractional part and leaving an 854px drawing at 1x in 1600px.
        const qreal fitted = qMin((1600.0 - 24) / 854, (900.0 - 24) / 480);
        QTRY_VERIFY(qAbs(win->property("zoom").toReal() - fitted) < 0.001);
        QVERIFY(win->property("zoom").toReal() > 1);

        // A document larger than the viewport still fits in full. The previous
        // 1x floor made View > Fit unable to fit these documents at all.
        document.reset(2048, 1080);
        QTRY_VERIFY(win->property("zoom").toReal() < 1);
        QVERIFY(surface->property("contentWidth").toReal() <= 1600 - 24 + 0.01);
        QVERIFY(surface->property("contentHeight").toReal() <= 900 - 24 + 0.01);
        document.reset(64, 64);
        view.resize(200, 200);
        QTRY_COMPARE(surface->width(), 200.0);

        // The surface fits the drawing to the pane on load, which for a 200px
        // viewport means a zoom with nothing to scroll to. Take the view over,
        // the way a person does the moment they zoom in, and give it somewhere
        // to go: 64 columns at 12x is 768 wide inside 200.
        surface->setProperty("touched", true);
        win->setProperty("zoom", 12.0);
        QVERIFY(surface->property("scrollsX").toBool());
        QVERIFY(surface->property("scrollsY").toBool());

        const qreal restingY = surface->property("panY").toReal();
        sendWheel(&view, Qt::NoModifier);
        QVERIFY2(!qFuzzyCompare(surface->property("panY").toReal(), restingY),
                 "a plain wheel did not scroll the surface");

        const qreal wasZoom = win->property("zoom").toReal();
        sendWheel(&view, Qt::ControlModifier);
        QVERIFY2(!qFuzzyCompare(win->property("zoom").toReal(), wasZoom),
                 "ctrl and the wheel did not zoom");

        const qreal thenZoom = win->property("zoom").toReal();
        sendWheel(&view, Qt::AltModifier);
        QVERIFY2(!qFuzzyCompare(win->property("zoom").toReal(), thenZoom),
                 "alt and the wheel did not zoom");

        // Deliberately give the pane dimensions that are not divisible by the
        // cell size. Its inner viewport drops the remainders instead of showing
        // half a pixel at an edge.
        document.reset(100, 100);
        view.resize(203, 207);
        QTRY_COMPARE(surface->width(), 203.0);
        QTRY_COMPARE(surface->height(), 207.0);
        win->setProperty("zoom", 4.0);
        surface->setProperty("touched", true);
        surface->setProperty("caretMarginX", 0);
        surface->setProperty("caretMarginY", 0);
        surface->setProperty("requestedPanX", 0.0);
        surface->setProperty("requestedPanY", 0.0);
        QVERIFY(QMetaObject::invokeMethod(surface, "clampPan"));
        QCOMPARE(surface->property("viewportWidth").toReal(), 200.0);
        QCOMPARE(surface->property("viewportHeight").toReal(), 204.0);
        QCOMPARE(surface->property("viewColumn").toReal(), 25.0);
        QCOMPARE(surface->property("viewRow").toReal(), 24.0);
        QCOMPARE(surface->property("viewColumns").toReal(), 50.0);
        QCOMPARE(surface->property("viewRows").toReal(), 51.0);
        auto *pixelViewport =
            surface->findChild<QQuickItem *>(QStringLiteral("pixelViewport"));
        auto *pixelStage =
            surface->findChild<QQuickItem *>(QStringLiteral("pixelStage"));
        QVERIFY(pixelViewport);
        QVERIFY(pixelStage);
        QCOMPARE(std::fmod(pixelViewport->width(), 4.0), 0.0);
        QCOMPARE(std::fmod(pixelViewport->height(), 4.0), 0.0);
        QCOMPARE(std::fmod(pixelStage->x(), 4.0), 0.0);
        QCOMPARE(std::fmod(pixelStage->y(), 4.0), 0.0);
        const auto reveal = [&](int column, int row) {
            return QMetaObject::invokeMethod(surface, "reveal",
                                             Q_ARG(QVariant, column),
                                             Q_ARG(QVariant, row));
        };
        const qreal alignedY = surface->property("panY").toReal();

        // Visible columns are [25, 75): 74 is the last one and may be used;
        // trying to walk to 75 is what recentres X. Y stays exactly put.
        QVERIFY(reveal(74, 50));
        QCOMPARE(surface->property("panX").toReal(), 0.0);
        QVERIFY(reveal(75, 50));
        QCOMPARE(surface->property("panX").toReal(), -100.0);
        QCOMPARE(surface->property("panY").toReal(), alignedY);

        // Twenty pixels from the right edge starts at column 55.
        surface->setProperty("requestedPanX", 0.0);
        surface->setProperty("caretMarginX", 20);
        QVERIFY(QMetaObject::invokeMethod(surface, "clampPan"));
        QVERIFY(reveal(54, 50));
        QCOMPARE(surface->property("panX").toReal(), 0.0);
        QVERIFY(reveal(55, 50));
        QCOMPARE(surface->property("panX").toReal(), -20.0);
        QCOMPARE(surface->property("panY").toReal(), alignedY);

        // Center can follow either axis without stealing the position chosen
        // for the other one.
        surface->setProperty("requestedPanX", 0.0);
        surface->setProperty("requestedPanY", 0.0);
        surface->setProperty("caretMarginX", QStringLiteral("center"));
        surface->setProperty("caretMarginY", 0);
        QVERIFY(QMetaObject::invokeMethod(surface, "clampPan"));
        QCOMPARE(surface->property("viewportWidth").toReal(), 196.0);
        QVERIFY(reveal(51, 50));
        QCOMPARE(surface->property("panX").toReal(), -6.0);
        QCOMPARE(surface->property("panY").toReal(), alignedY);

        surface->setProperty("requestedPanX", 0.0);
        surface->setProperty("requestedPanY", 0.0);
        surface->setProperty("caretMarginX", 0);
        surface->setProperty("caretMarginY", QStringLiteral("center"));
        QVERIFY(QMetaObject::invokeMethod(surface, "clampPan"));
        QVERIFY(reveal(50, 51));
        QCOMPARE(surface->property("panX").toReal(), 0.0);
        QCOMPARE(surface->property("panY").toReal(), -6.0);

        // Smooth devices report sub-cell deltas. They accumulate in requested
        // pan, while the stage shown on screen moves only at a grid boundary.
        surface->setProperty("caretMarginY", 0);
        surface->setProperty("requestedPanX", 0.0);
        surface->setProperty("requestedPanY", 0.0);
        QVERIFY(QMetaObject::invokeMethod(surface, "clampPan"));
        for (int i = 0; i < 3; ++i)
            QVERIFY(QMetaObject::invokeMethod(surface, "scrollBy",
                                              Q_ARG(QVariant, 1),
                                              Q_ARG(QVariant, 0)));
        QVERIFY(qAbs(surface->property("requestedPanX").toReal() - 2.1) < 0.0001);
        QCOMPARE(surface->property("panX").toReal(), 4.0);
        QCOMPARE(std::fmod(pixelStage->x(), 4.0), 0.0);
        QCOMPARE(std::fmod(pixelStage->y(), 4.0), 0.0);
    }

    void aWheelOverTheWholeWindowReachesTheSurface()
    {
        QTest::failOnWarning();
        // Loads Main.qml -- the whole composition, rails and timeline and all --
        // and scrolls at a point inside the drawing pane. Testing Surface.qml
        // on its own passed while the studio did not, which means the thing
        // that swallows the wheel is something Surface does not contain.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(64, 64);
        Theme theme;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        static InputLog silent(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &silent);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        QVERIFY(root);
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(900, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QQuickItem *surface = window->findChild<QQuickItem *>(QStringLiteral("stage"));
        QVERIFY2(surface, "the drawing surface was not found in the window");

        root->setProperty("zoom", 12.0);
        surface->setProperty("touched", true);
        QVERIFY(surface->property("scrollsY").toBool());

        // A point well inside the middle pane: past the 232px left rail, below
        // the head, above the timeline.
        const QPointF inside(450, 350);
        const qreal restingY = surface->property("panY").toReal();
        QWindowSystemInterface::handleWheelEvent(window, inside,
                                                 window->mapToGlobal(inside),
                                                 QPoint(0, 0), QPoint(0, -120),
                                                 Qt::NoModifier);
        QCoreApplication::processEvents();
        QVERIFY2(!qFuzzyCompare(surface->property("panY").toReal(), restingY),
                  "a wheel over the drawing did not scroll it");

        // Painting is a content change, not a request to reopen the view. In
        // particular, applying a number key to a selection must preserve the
        // zoom and pan the artist chose instead of silently running Fit.
        document.setSelection(2, 3, 4, 5);
        const qreal chosenZoom = root->property("zoom").toReal();
        const qreal chosenPanX = surface->property("panX").toReal();
        const qreal chosenPanY = surface->property("panY").toReal();
        const QString first = root->property("registers").toStringList().value(0);
        QTest::keyClick(window, Qt::Key_1);
        QCOMPARE(document.slotAt(2, 3), first);
        QCOMPARE(document.slotAt(4, 5), first);
        QCOMPARE(root->property("zoom").toReal(), chosenZoom);
        QCOMPARE(surface->property("panX").toReal(), chosenPanX);
        QCOMPARE(surface->property("panY").toReal(), chosenPanY);
        QCOMPARE(surface->property("touched").toBool(), true);
    }

    void dirtyDocumentsGateEveryDestructiveWindowAction()
    {
        QTest::failOnWarning();
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");
        QQmlEngine engine;
        DocumentModel document;
        Theme theme;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        static InputLog silent(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &silent);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        QVERIFY(root);
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);

        document.paint(0, 0, QStringLiteral("I"));
        QVERIFY(document.dirty());
        const int columns = document.columns();
        const int rows = document.rows();
        for (const QString &action : {QStringLiteral("new"), QStringLiteral("open"),
                                      QStringLiteral("quit")}) {
            root->setProperty("pendingAction", QString());
            QVERIFY(QMetaObject::invokeMethod(root.data(), "requestAction",
                                              Q_ARG(QVariant, action)));
            QCOMPARE(root->property("pendingAction").toString(), action);
            QVERIFY(document.dirty());
            QCOMPARE(document.columns(), columns);
            QCOMPARE(document.rows(), rows);
        }

        root->setProperty("pendingAction", QString());
        window->close();
        QCoreApplication::processEvents();
        QCOMPARE(root->property("pendingAction").toString(), QStringLiteral("close"));
        QVERIFY(document.dirty());
    }

    void replacingASlotReachesEveryFrame()
    {
        // A colour belongs to the document, not to the frame you happen to be
        // looking at. Replacing it in one frame of twelve leaves an animation
        // that flickers between two colours, which is never what anybody meant.
        Document doc = Document::blank(4, 2);
        doc.addClip(QStringLiteral("walk"));
        doc.addFrame(QStringLiteral("walk"), 0, false);

        Grid one = doc.frame(QStringLiteral("idle"), 0);
        ops::paint(one, 0, 0, u'R');
        ops::paint(one, 1, 0, u'R');
        doc.setFrame(QStringLiteral("idle"), 0, one);

        Grid two = doc.frame(QStringLiteral("walk"), 1);
        ops::paint(two, 3, 1, u'R');
        doc.setFrame(QStringLiteral("walk"), 1, two);

        QCOMPARE(doc.replaceSlot(u'R', u'B'), 3);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).at(0, 0), QChar(u'B'));
        QCOMPARE(doc.frame(QStringLiteral("walk"), 1).at(3, 1), QChar(u'B'));
        QVERIFY(!doc.usesSlot(u'R'));
        QVERIFY(doc.usesSlot(u'B'));

        // Replacing a slot with itself is not an edit.
        QCOMPARE(doc.replaceSlot(u'B', u'B'), 0);

        // And one frame at a time, for when the whole animation is not what
        // you meant. Recolouring all twelve frames by accident is a bigger
        // mistake than recolouring one, and a quieter one.
        Grid three = doc.frame(QStringLiteral("idle"), 0);
        ops::paint(three, 0, 1, u'G');
        doc.setFrame(QStringLiteral("idle"), 0, three);
        Grid four = doc.frame(QStringLiteral("walk"), 0);
        ops::paint(four, 0, 1, u'G');
        doc.setFrame(QStringLiteral("walk"), 0, four);

        QCOMPARE(doc.replaceSlotInFrame(QStringLiteral("idle"), 0, u'G', u'Y'), 1);
        QCOMPARE(doc.frame(QStringLiteral("idle"), 0).at(0, 1), QChar(u'Y'));
        QCOMPARE(doc.frame(QStringLiteral("walk"), 0).at(0, 1), QChar(u'G'));
    }

    void thePaletteGoesWellPastTheAlphabet()
    {
        // A slot is one character -- that is the format. One character is far
        // more than the letters and digits, though, and stopping at sixty-two
        // was a limit nobody chose on purpose.
        DocumentModel doc;
        QSet<QString> handed;
        for (int i = 0; i < 300; ++i) {
            const QString slot = doc.freeSlot();
            QVERIFY2(!slot.isEmpty(), "ran out of slots before three hundred");
            QCOMPARE(slot.size(), 1);
            QVERIFY(slot != QStringLiteral("."));      // emptiness is not a slot
            // The digits belong to the studio's colour keys. A slot called `3`
            // whose colour is not what pressing 3 draws is a contradiction
            // sitting on screen.
            QVERIFY2(!slot.at(0).isDigit(), "a digit was handed out as a slot");
            QVERIFY(slot != QStringLiteral("\""));     // and these two would
            QVERIFY(slot != QStringLiteral("\\"));     // turn a row into escapes
            QVERIFY2(!handed.contains(slot), "the same slot was handed out twice");
            handed.insert(slot);
            doc.setPaletteColour(slot, QStringLiteral("#123456"));
        }

        // Letters and digits come first, so a palette that stays small stays
        // legible in the file.
        DocumentModel fresh;
        QCOMPARE(fresh.freeSlot(), QStringLiteral("J"));   // the standard palette's first gap
    }

    void russianRouletteDrawsFromTheWholeOfRgb()
    {
        DocumentModel doc;
        QSet<QString> seen;
        for (int i = 0; i < 40; ++i) {
            const QString hex = doc.randomColour();
            QCOMPARE(hex.size(), 7);
            QVERIFY(hex.startsWith(QLatin1Char('#')));
            QVERIFY(QColor(hex).isValid());
            seen.insert(hex);
        }
        // Genuinely random, which is the point: drawing from what is already
        // in the palette would be a shuffle, and a shuffle is not a gamble.
        QVERIFY2(seen.size() > 30, "the same few colours keep coming back");
    }

    void coloursCanBeFoundByName()
    {
        DocumentModel doc;

        // The point of the thing: somebody who wants "a teal" should not have
        // to compose one out of three numbers.
        const QVariantList teals = doc.findColours(QStringLiteral("teal"));
        QVERIFY(!teals.isEmpty());
        QVERIFY(teals.first().toMap().value(QStringLiteral("name")).toString()
                    .contains(QStringLiteral("teal"), Qt::CaseInsensitive));

        // A hex comes back first, under what was typed, so there is no need
        // for a second field that only takes hexes.
        const QVariantList hex = doc.findColours(QStringLiteral("#3AA9C0"));
        QVERIFY(!hex.isEmpty());
        QCOMPARE(hex.first().toMap().value(QStringLiteral("colour")).toString(),
                 QStringLiteral("#3AA9C0"));

        // Nothing typed lists them all, which is what opening the panel does.
        QVERIFY(doc.findColours(QString()).size() > 40);

        // And a search that matches nothing says so by being empty rather than
        // by falling back to something arbitrary.
        QVERIFY(doc.findColours(QStringLiteral("zzzz")).isEmpty());
    }

    void theCursorTakesAColourThatShowsAgainstThePixelUnderIt()
    {
        DocumentModel doc;
        doc.setPaletteColour(QStringLiteral("W"), QStringLiteral("#ffffff"));
        doc.setPaletteColour(QStringLiteral("K"), QStringLiteral("#000000"));
        doc.setPaletteColour(QStringLiteral("G"), QStringLiteral("#808080"));

        // An empty pixel shows the chequerboard, which is the window's, not
        // the document's: no opinion offered.
        QVERIFY(!doc.contrastAt(1, 1).isValid());

        doc.paint(1, 1, QStringLiteral("W"));
        QCOMPARE(doc.contrastAt(1, 1), QColor(Qt::black));
        doc.paint(2, 1, QStringLiteral("K"));
        QCOMPARE(doc.contrastAt(2, 1), QColor(Qt::white));

        // The one that matters: inverting a mid grey gives another mid grey,
        // and a cursor drawn in it vanishes exactly where a drawing is busiest.
        doc.paint(3, 1, QStringLiteral("G"));
        const QColor against = doc.contrastAt(3, 1);
        const auto lum = [](const QColor &c) {
            return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
        };
        QVERIFY2(qAbs(lum(against) - lum(QColor(QStringLiteral("#808080")))) > 0.28,
                  "the cursor colour is as bright as the pixel it sits on");
    }

    void goToPixelMovesByExactZeroBasedCoordinates()
    {
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(32, 24);
        Theme theme;
        static InputLog mute(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &mute);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1000, 720);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *sheet = root->findChild<QObject *>(QStringLiteral("goToSheet"));
        auto *xField = root->findChild<QObject *>(QStringLiteral("goToX"));
        auto *yField = root->findChild<QObject *>(QStringLiteral("goToY"));
        QVERIFY(sheet);
        QVERIFY(xField);
        QVERIFY(yField);
        auto *xInput = xField->property("input").value<QObject *>();
        auto *yInput = yField->property("input").value<QObject *>();
        QVERIFY(xInput);
        QVERIFY(yInput);

        document.setSelection(1, 1, 2, 2);
        QTest::keyClick(window, Qt::Key_G);
        QTRY_VERIFY(sheet->property("opened").toBool());
        QTRY_VERIFY(xInput->property("activeFocus").toBool());
        QTest::keyClick(window, Qt::Key_1);
        QTest::keyClick(window, Qt::Key_7);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(yInput->property("activeFocus").toBool());
        QTest::keyClick(window, Qt::Key_1);
        QTest::keyClick(window, Qt::Key_3);
        QTest::keyClick(window, Qt::Key_Return);

        QTRY_VERIFY(!sheet->property("opened").toBool());
        QCOMPARE(root->property("caretColumn").toInt(), 17);
        QCOMPARE(root->property("caretRow").toInt(), 13);
        QVERIFY(!document.hasSelection());

        // An out-of-range coordinate leaves both the cursor and panel in place.
        QTest::keyClick(window, Qt::Key_G);
        QTRY_VERIFY(xInput->property("activeFocus").toBool());
        QTest::keyClick(window, Qt::Key_3);
        QTest::keyClick(window, Qt::Key_2);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(yInput->property("activeFocus").toBool());
        QTest::keyClick(window, Qt::Key_0);
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(sheet->property("opened").toBool());
        QVERIFY(!sheet->property("problem").toString().isEmpty());
        QCOMPARE(root->property("caretColumn").toInt(), 17);
        QCOMPARE(root->property("caretRow").toInt(), 13);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!sheet->property("opened").toBool());
        QCOMPARE(root->property("caretColumn").toInt(), 17);
        QCOMPARE(root->property("caretRow").toInt(), 13);
    }

    void theArrowKeysWalkAPixelCursorOverTheDrawing()
    {
        // Drawing with a mouse is fine for shapes and hopeless for placing one
        // pixel exactly. This asserts the keys actually reach the canvas --
        // which is a question about focus, and focus is the thing a window full
        // of controls quietly takes away.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(32, 24);
        Theme theme;
        static InputLog mute(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &mute);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1000, 720);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QCOMPARE(root->property("caretColumn").toInt(), -1);

        // First press puts the cursor in the middle rather than a corner.
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(root->property("caretColumn").toInt(), 16);
        QCOMPARE(root->property("caretRow").toInt(), 12);

        // A document pixel can occupy less than one screen pixel after Fit.
        // The keyboard cursor stays visible even though its logical movement
        // remains exactly one document cell.
        auto *keyboardCursor =
            root->findChild<QQuickItem *>(QStringLiteral("keyboardCursor"));
        QVERIFY(keyboardCursor);
        const qreal initialZoom = root->property("zoom").toReal();
        root->setProperty("zoom", 0.5);
        QTRY_COMPARE(keyboardCursor->width(), 7.0);
        root->setProperty("zoom", initialZoom);

        QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(root->property("caretColumn").toInt(), 17);
        QCOMPARE(root->property("caretRow").toInt(), 13);

        // Shift fixes an anchor and extends an inclusive rectangle.
        QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(root->property("caretColumn").toInt(), 16);
        QVERIFY(document.hasSelection());
        QCOMPARE(document.selectionX(), 16);
        QCOMPARE(document.selectionY(), 13);
        QCOMPARE(document.selectionWidth(), 2);
        QCOMPARE(document.selectionHeight(), 1);

        QTest::keyClick(window, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(document.selectionX(), 16);
        QCOMPARE(document.selectionY(), 12);
        QCOMPARE(document.selectionWidth(), 2);
        QCOMPARE(document.selectionHeight(), 2);

        // Both modifiers keep selecting, but by the configured big step.
        QTest::keyClick(window, Qt::Key_Left,
                        Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(root->property("caretColumn").toInt(), 8);
        QCOMPARE(document.selectionX(), 8);
        QCOMPARE(document.selectionWidth(), 10);

        // Ctrl keeps the old big-step navigation and plain movement clears the
        // range before it walks.
        QTest::keyClick(window, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(root->property("caretColumn").toInt(), 16);
        QVERIFY(!document.hasSelection());
        root->setProperty("caretColumn", 9);
        root->setProperty("caretRow", 13);

        // Focus commands prefer the selected rectangle. A number paints every
        // selected cell and Delete erases exactly those cells, without dropping
        // the range.
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(window, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(document.selectionX(), 9);
        QCOMPARE(document.selectionY(), 13);
        QCOMPARE(document.selectionWidth(), 2);
        QCOMPARE(document.selectionHeight(), 2);
        const QString selectedSlot = root->property("registers").toStringList().value(0);
        QTest::keyClick(window, Qt::Key_1);
        for (int y = 13; y <= 14; ++y)
            for (int x = 9; x <= 10; ++x)
                QCOMPARE(document.slotAt(x, y), selectedSlot);
        QVERIFY(document.hasSelection());

        QTest::keyClick(window, Qt::Key_Delete);
        for (int y = 13; y <= 14; ++y)
            for (int x = 9; x <= 10; ++x)
                QCOMPARE(document.slotAt(x, y), QStringLiteral("."));
        QVERIFY(document.hasSelection());

        QTest::keyClick(window, Qt::Key_Return);
        for (int y = 13; y <= 14; ++y)
            for (int x = 9; x <= 10; ++x)
                QCOMPARE(document.slotAt(x, y), selectedSlot);
        QTest::keySequence(window, QKeySequence::Copy);
        QTest::keyClick(window, Qt::Key_Backspace);
        for (int y = 13; y <= 14; ++y)
            for (int x = 9; x <= 10; ++x)
                QCOMPARE(document.slotAt(x, y), QStringLiteral("."));
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!document.hasSelection());
        root->setProperty("caretColumn", 20);
        root->setProperty("caretRow", 2);
        QTest::keySequence(window, QKeySequence::Paste);
        for (int y = 2; y <= 3; ++y)
            for (int x = 20; x <= 21; ++x)
                QCOMPARE(document.slotAt(x, y), selectedSlot);
        QCOMPARE(document.selectionX(), 20);
        QCOMPARE(document.selectionY(), 2);
        QTest::keySequence(window, QKeySequence::Undo);
        document.clearSelection();
        root->setProperty("caretColumn", 9);
        root->setProperty("caretRow", 13);

        // And it draws where it stands.
        QCOMPARE(document.slotAt(9, 13), QStringLiteral("."));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(document.slotAt(9, 13), root->property("slot").toString());

        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(document.slotAt(9, 13), QStringLiteral("."));

        // Clearing the whole frame moved to Ctrl+Delete.
        QTest::keyClick(window, Qt::Key_Return);
        root->setProperty("caretColumn", 5);
        root->setProperty("caretRow", 5);
        QTest::keyClick(window, Qt::Key_Return);
        QTest::keyClick(window, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(document.slotAt(9, 13), QStringLiteral("."));
        QCOMPARE(document.slotAt(5, 5), QStringLiteral("."));

        // The cursor stays inside the drawing however long you lean on a key.
        for (int i = 0; i < 40; ++i)
            QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(root->property("caretRow").toInt(), 0);

        // The leader: a key that says the next one names a colour. Needed
        // because every letter is already a tool or a toggle, so the letters
        // that name palette slots had nowhere to go.
        QTest::keyClick(window, Qt::Key_Semicolon);
        QCOMPARE(root->property("awaitingSlot").toBool(), true);
        // Shift, because slots tell the cases apart: this palette has B and
        // not b, and a studio that quietly accepted either would pick the
        // wrong colour in any document that uses both.
        QTest::keyClick(window, Qt::Key_B, Qt::ShiftModifier);   // normally the pencil
        QCOMPARE(root->property("awaitingSlot").toBool(), false);
        QCOMPARE(root->property("slot").toString(), QStringLiteral("B"));
        QCOMPARE(document.slotAt(root->property("caretColumn").toInt(),
                                 root->property("caretRow").toInt()),
                 QStringLiteral("B"));

        // A letter no slot uses changes nothing, and says so.
        QTest::keyClick(window, Qt::Key_Semicolon);
        QTest::keyClick(window, Qt::Key_Slash);
        QCOMPARE(root->property("slot").toString(), QStringLiteral("B"));
        QVERIFY(document.note().contains(QStringLiteral("no slot")));

        // And escape backs out of it without painting anything.
        QTest::keyClick(window, Qt::Key_Semicolon);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(root->property("awaitingSlot").toBool(), false);
        QCOMPARE(root->property("caretColumn").toInt() >= 0, true);

        // Draw mode. Entered with a key and left with Escape -- a toggle whose
        // state you have to remember is a toggle you get wrong. Inside it the
        // arrows paint only while a colour is held: moving and drawing are the
        // same gesture with and without your other hand on a number.
        root->setProperty("caretColumn", 4);
        root->setProperty("caretRow", 4);
        QTest::keyClick(window, Qt::Key_D);
        QCOMPARE(root->property("mode").toString(), QStringLiteral("draw"));

        // Moving alone leaves the drawing alone.
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(document.slotAt(5, 4), QStringLiteral("."));

        const QString first = root->property("registers").toStringList().value(0);
        QTest::keyPress(window, Qt::Key_1);          // and hold it
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(window, Qt::Key_Right);
        QTest::keyRelease(window, Qt::Key_1);
        for (int x = 5; x <= 9; ++x)
            QCOMPARE(document.slotAt(x, 4), first);

        // Letting go ends the run, and the undo step with it: one press takes
        // back everything that one press drew.
        QTest::keySequence(window, QKeySequence::Undo);
        for (int x = 5; x <= 9; ++x)
            QCOMPARE(document.slotAt(x, 4), QStringLiteral("."));

        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(root->property("mode").toString(), QString());

        // A line is a set of corners and nothing is drawn until a colour is
        // named, so a right angle is two corners and one press rather than two
        // lines whose ends have to be lined up by hand.
        root->setProperty("caretColumn", 2);
        root->setProperty("caretRow", 10);
        QTest::keyClick(window, Qt::Key_L);
        for (int i = 0; i < 6; ++i)
            QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_L);          // the corner
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(document.slotAt(5, 10), QStringLiteral("."));   // still nothing

        QTest::keyClick(window, Qt::Key_1);          // draw it, in that colour
        QCOMPARE(root->property("linePoints").toList().size(), 0);
        for (int x = 2; x <= 8; ++x)
            QCOMPARE(document.slotAt(x, 10), first);
        for (int y = 10; y <= 13; ++y)
            QCOMPARE(document.slotAt(8, y), first);

        // The whole shape is one undo step: it was one line as far as the
        // person drawing it was concerned.
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(document.slotAt(5, 10), QStringLiteral("."));
        QCOMPARE(document.slotAt(8, 13), QStringLiteral("."));

        // Escape throws away a line that has not been drawn.
        QTest::keyClick(window, Qt::Key_L);
        QCOMPARE(root->property("linePoints").toList().size(), 1);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(root->property("linePoints").toList().size(), 0);

        // Pick mode: the digits stop choosing a colour and start collecting
        // one. Point at a pixel, press a number, that colour is on that number.
        root->setProperty("caretColumn", 6);
        root->setProperty("caretRow", 6);
        root->setProperty("slot", QStringLiteral("R"));
        QTest::keyClick(window, Qt::Key_Return);              // put an R there
        QCOMPARE(document.slotAt(6, 6), QStringLiteral("R"));

        QTest::keyClick(window, Qt::Key_P);
        QCOMPARE(root->property("mode").toString(), QStringLiteral("pick"));
        QTest::keyClick(window, Qt::Key_4);
        QCOMPARE(root->property("registers").toStringList().value(3), QStringLiteral("R"));
        // ... and it did not paint while picking.
        QCOMPARE(document.slotAt(6, 6), QStringLiteral("R"));

        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(root->property("mode").toString(), QString());

        // roleta russa: paints with a colour nobody chose. It ADDS a slot like
        // every other way a colour arrives here -- the gamble is which colour
        // you get, not which of the ones already in the drawing gets ruined.
        root->setProperty("caretColumn", 12);
        root->setProperty("caretRow", 12);
        const QVariantList palette = document.palette();
        QTest::keyClick(window, Qt::Key_R);

        QCOMPARE(document.palette().size(), palette.size() + 1);
        for (int i = 0; i < palette.size(); ++i) {
            QCOMPARE(document.palette().at(i).toMap().value(QStringLiteral("colour")),
                     palette.at(i).toMap().value(QStringLiteral("colour")));
        }
        const QString shot = root->property("slot").toString();
        QCOMPARE(document.slotAt(12, 12), shot);
        QVERIFY(document.colourOf(shot).isValid());

        // And it is one undo step, like anything else that paints.
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(document.slotAt(12, 12), QStringLiteral("."));

        // Focus can be taken away by anything in the window, so there is a way
        // to take it back. Constructing the theft reliably in a test proved
        // beyond me -- the first attempt focused a field inside a closed dialog
        // and passed while the bug was real -- so this asserts only the recovery
        // itself, and the window calls it whenever the drawing is pressed.
        QMetaObject::invokeMethod(root.data(), "focusCanvas");
        const int before = root->property("caretColumn").toInt();
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(root->property("caretColumn").toInt(), before + 1);

        // Holding Shift turns a left drag into a rectangle instead of a
        // stroke. Coordinates are local pixels scaled by the current zoom.
        auto *pointer = root->findChild<QQuickItem *>(QStringLiteral("canvasPointer"));
        QVERIFY(pointer);
        const qreal zoom = root->property("zoom").toReal();
        const QPoint from = pointer->mapToScene(QPointF(2.5 * zoom, 3.5 * zoom)).toPoint();
        const QPoint to = pointer->mapToScene(QPointF(5.5 * zoom, 7.5 * zoom)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, from);
        QTest::mouseMove(window, to);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier, to);
        QVERIFY(document.hasSelection());
        QCOMPARE(document.selectionX(), 2);
        QCOMPARE(document.selectionY(), 3);
        QCOMPARE(document.selectionWidth(), 4);
        QCOMPARE(document.selectionHeight(), 5);
    }

    void pickupAndNumberShortcutsPaintASelectionWithTheCollectedColour()
    {
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(12, 12);
        Theme theme;
        static InputLog mute(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &mute);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1000, 720);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        root->setProperty("caretColumn", 4);
        root->setProperty("caretRow", 4);
        root->setProperty("slot", QStringLiteral("R"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(document.slotAt(4, 4), QStringLiteral("R"));

        // P and 4 collect the focused colour. Navigation alone deliberately
        // leaves pickup active.
        QTest::keyClick(window, Qt::Key_P);
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(root->property("mode").toString(), QStringLiteral("pick"));
        QTest::keyClick(window, Qt::Key_Left);
        QTest::keyClick(window, Qt::Key_4);
        QCOMPARE(root->property("registers").toStringList().value(3),
                 QStringLiteral("R"));

        // Starting a selection exits pickup, so 4 paints rather than replacing
        // the register with the endpoint's colour.
        QTest::keyClick(window, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(root->property("mode").toString(), QString());
        QCOMPARE(document.selectionX(), 4);
        QCOMPARE(document.selectionWidth(), 2);
        QTest::keyClick(window, Qt::Key_4);
        QCOMPARE(document.slotAt(4, 4), QStringLiteral("R"));
        QCOMPARE(document.slotAt(5, 4), QStringLiteral("R"));

        // Painting emits fileChanged, but that must not refill the number keys.
        QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_4);
        QCOMPARE(document.slotAt(6, 4), QStringLiteral("R"));
        QCOMPARE(root->property("registers").toStringList().value(3),
                 QStringLiteral("R"));
    }

    void everyControlIsReachableWithTab()
    {
        // The audit this came from: nothing but the text fields accepted focus,
        // and Tab was intercepted to jump back to the drawing -- which answered
        // one complaint by making every button in the window unreachable.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        Theme theme;
        static InputLog still(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &still);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1200, 860);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // Walk the window with Tab and collect what the focus lands on.
        QSet<QQuickItem *> visited;
        QStringList kinds;
        for (int i = 0; i < 60; ++i) {
            QTest::keyClick(window, Qt::Key_Tab);
            QQuickItem *here = window->activeFocusItem();
            if (!here)
                continue;
            if (!visited.contains(here)) {
                visited.insert(here);
                kinds << QString::fromUtf8(here->metaObject()->className());
            }
        }
        qInfo("tab reached %lld controls", qint64(visited.size()));

        QVERIFY2(visited.size() > 12,
                 "Tab walks past almost everything: the controls do not take focus");

        // The menu bar answers to the keyboard too. Nothing in the window
        // should need a pointer, and a menu you can only open by clicking is
        // the whole command set behind one.
        QTest::keyClick(window, Qt::Key_F10);
        QTest::qWait(30);
        QQuickItem *onMenus = window->activeFocusItem();
        QVERIFY2(onMenus && (onMenus->inherits("QQuickMenuBar")
                             || onMenus->inherits("QQuickMenuBarItem")
                             || (onMenus->parentItem()
                                 && onMenus->parentItem()->inherits("QQuickMenuBar"))),
                 "F10 did not put the keyboard on the menu bar");

        // Escape brings the keyboard back to the drawing from wherever Tab
        // left it, so getting lost is one key rather than a hunt.
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY2(root->property("caretColumn").toInt() >= 0,
                 "escape did not hand the keyboard back to the drawing");
    }

    void theColourPanelIsDrivenFromTheKeyboardAlone()
    {
        // Opening a search panel and then having to reach for the mouse to
        // type in it defeats the point of having it on a key.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(16, 16);
        Theme theme;
        static InputLog quiet2(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &quiet2);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1100, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QTest::keyClick(window, Qt::Key_C);
        QTest::qWait(50);

        // The keyboard has to be IN the search box, not merely near it.
        QQuickItem *focused = window->activeFocusItem();
        QVERIFY2(focused && focused->inherits("QQuickTextInput"),
                 "the colour panel opened without the keyboard in its search box");

        // Typing filters, and the arrows walk the matches without leaving the
        // box: searching and choosing are one gesture rather than two.
        for (QChar c : QStringLiteral("teal"))
            QTest::keyClick(window, c.toLatin1());
        QTest::qWait(50);
        QTest::keyClick(window, Qt::Key_Down);
        QTest::qWait(20);

        const QString picked = root->property("chosenColour").toString();
        QVERIFY(!picked.isEmpty());
        const QVariantList before = document.palette();

        // Return settles the colour; the panel then waits to be told which
        // number key it goes on. Two steps because the digits are needed for
        // typing a hex until the colour is chosen, and one key cannot mean two
        // things at once.
        QTest::keyClick(window, Qt::Key_Return);
        QTest::qWait(50);

        QTest::keyClick(window, Qt::Key_3);
        QTest::qWait(50);

        // The drawing must not have changed. A slot is not a swatch: every
        // pixel drawn with it refers to it, so recolouring one repaints all of
        // them at once. Reaching for a nicer blue must not repaint the sky.
        for (int i = 0; i < before.size(); ++i) {
            QCOMPARE(document.palette().at(i).toMap().value(QStringLiteral("colour")),
                     before.at(i).toMap().value(QStringLiteral("colour")));
        }
        QCOMPARE(document.palette().size(), before.size() + 1);

        // The digit now carries the colour, and drawing switches to it.
        const QString letter = root->property("registers").toStringList().value(2);
        QVERIFY(!letter.isEmpty());
        QCOMPARE(document.colourOf(letter).name(QColor::HexRgb).toUpper(), picked);
        QCOMPARE(root->property("slot").toString(), letter);
    }

    void replaceRepaintsOneColourAndLeavesTheOthersAlone()
    {
        DocumentModel doc;
        doc.reset(8, 8);
        doc.addFrame(true);                       // two frames

        doc.paint(1, 1, QStringLiteral("R"));
        doc.setFrame(0);
        doc.paint(2, 2, QStringLiteral("R"));
        doc.paint(3, 3, QStringLiteral("B"));     // a bystander
        QCOMPARE(doc.countSlot(QStringLiteral("R"), true), 2);
        QCOMPARE(doc.countSlot(QStringLiteral("R"), false), 1);   // this frame

        const QColor bystander = doc.colourOf(QStringLiteral("B"));
        doc.replaceColour(QStringLiteral("R"), QStringLiteral("#00FF00"), true);

        // Every frame, because a colour belongs to the document and not to the
        // frame you happen to be looking at.
        const QString now = doc.slotAt(2, 2);
        QCOMPARE(doc.colourOf(now), QColor(QStringLiteral("#00FF00")));
        doc.setFrame(1);
        QCOMPARE(doc.slotAt(1, 1), now);

        // The bystander keeps its colour: "every pixel of this colour" is what
        // was asked for, not "every pixel that shares a slot with it".
        doc.setFrame(0);
        QCOMPARE(doc.colourOf(QStringLiteral("B")), bystander);
        QCOMPARE(doc.slotAt(3, 3), QStringLiteral("B"));

        // And the emptied slot does not sit in the palette forever.
        bool stillThere = false;
        for (const QVariant &entry : doc.palette())
            stillThere = stillThere
                         || entry.toMap().value(QStringLiteral("slot")) == QStringLiteral("R");
        QVERIFY2(!stillThere, "the replaced slot was left in the palette");

        // One undo step, like anything else that paints.
        doc.undo();
        doc.setFrame(0);
        QCOMPARE(doc.slotAt(2, 2), QStringLiteral("R"));
    }

    void replaceCanBeHeldToOneFrame()
    {
        // Both scopes are wanted and neither is the obvious default. The
        // window puts them one keystroke apart -- Enter and shift-Enter --
        // rather than choosing on the person's behalf.
        DocumentModel doc;
        doc.reset(8, 8);
        doc.paint(1, 1, QStringLiteral("R"));
        doc.addFrame(true);                       // a copy, so both hold an R
        QCOMPARE(doc.countSlot(QStringLiteral("R"), true), 2);
        QCOMPARE(doc.countSlot(QStringLiteral("R"), false), 1);

        doc.replaceColour(QStringLiteral("R"), QStringLiteral("#00FF00"), false);

        // The frame that was open changed, and the other one did not.
        const QString now = doc.slotAt(1, 1);
        QVERIFY(now != QStringLiteral("R"));
        QCOMPARE(doc.colourOf(now), QColor(QStringLiteral("#00FF00")));
        doc.setFrame(0);
        QCOMPARE(doc.slotAt(1, 1), QStringLiteral("R"));

        // R is still in use, so it stays in the palette. Dropping it here
        // would take the colour out from under the frame that still draws it.
        bool kept = false;
        for (const QVariant &entry : doc.palette())
            kept = kept || entry.toMap().value(QStringLiteral("slot"))
                               == QStringLiteral("R");
        QVERIFY2(kept, "a slot still in use was dropped from the palette");
    }

    void drawingCostsTheSameWhateverThePaletteHolds()
    {
        // `Palette::colour` is called once per pixel by the renderer. While it
        // was a linear scan, a document with three hundred slots drew a hundred
        // times slower than one with three, for no reason a person could see --
        // and the studio renders the canvas, three previews and one thumbnail
        // per frame on every change.
        //
        // Wall-clock, but the ratio being measured is around a hundred if the
        // lookup is linear, so noise cannot hide it.
        const auto timeOne = [](int extraSlots) {
            Document doc = Document::blank(64, 64);
            Grid grid = doc.frame(doc.clipNames().value(0), 0);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    ops::paint(grid, x, y, u'I');
            doc.setFrame(doc.clipNames().value(0), 0, grid);

            for (int i = 0; i < extraSlots; ++i)
                doc.palette().set(QChar(0x100 + i), QColor(Qt::red));

            QElapsedTimer clock;
            clock.start();
            for (int i = 0; i < 40; ++i)
                render::toImage(doc, doc.clipNames().value(0), 0, {});
            return clock.nsecsElapsed();
        };

        const qint64 small = timeOne(0);
        const qint64 large = timeOne(300);
        qInfo("forty renders: %lld us with a small palette, %lld us with 300 slots",
              small / 1000, large / 1000);
        QVERIFY2(large < small * 4 + 2000000,
                 "rendering slows down as the palette grows");
    }

    void fullHdFilesOpenWithinBudget_data()
    {
        QTest::addColumn<int>("frameCount");
        QTest::addColumn<qint64>("budgetMs");
        QTest::newRow("single-frame") << 1 << qint64(500);
        QTest::newRow("three-frame-animation") << 3 << qint64(1500);
    }

    void fullHdFilesOpenWithinBudget()
    {
        QFETCH(int, frameCount);
        QFETCH(qint64, budgetMs);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("full-hd.json"));
        QVERIFY(Codec::writeFile(path, fullHdDocument(frameCount)));

        DocumentModel document;
        QElapsedTimer clock;
        clock.start();
        const bool opened = document.open(QUrl::fromLocalFile(path).toString());
        const qint64 elapsed = clock.elapsed();

        QVERIFY(opened);
        QCOMPARE(document.columns(), 1920);
        QCOMPARE(document.rows(), 1080);
        QCOMPARE(document.frameCount(), frameCount);
        qInfo("opened %lld KiB Full HD file with %d frame(s) in %lld ms",
              QFileInfo(path).size() / 1024, frameCount, elapsed);
        QVERIFY2(elapsed < budgetMs,
                 qPrintable(QStringLiteral("opening took %1 ms; budget is %2 ms")
                                .arg(elapsed)
                                .arg(budgetMs)));
    }

    void fullHdStudioRasterStaysWithinBudget_data()
    {
        QTest::addColumn<int>("frameCount");
        QTest::addColumn<qint64>("budgetMs");
        QTest::newRow("single-frame") << 1 << qint64(250);
        QTest::newRow("three-frame-animation") << 3 << qint64(500);
    }

    void fullHdStudioRasterStaysWithinBudget()
    {
        QFETCH(int, frameCount);
        QFETCH(qint64, budgetMs);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("full-hd-animation.json"));
        QVERIFY(Codec::writeFile(path, fullHdDocument(frameCount)));
        const Codec::Result read = Codec::readFile(path);
        QVERIFY(read);

        // A conservative upper bound for initial paint: one checker layer, one
        // current-frame layer, and one thumbnail per supplied frame. The real
        // timeline is virtualized, so longer animations do not create every
        // thumbnail at once.
        render::Options checker;
        checker.checker = true;
        QElapsedTimer clock;
        clock.start();
        QVERIFY(!render::toImage(read.document, QStringLiteral("idle"), 0,
                                 checker).isNull());
        QVERIFY(!render::toImage(read.document, QStringLiteral("idle"), 0, {})
                     .isNull());
        for (int frame = 0; frame < frameCount; ++frame) {
            QVERIFY(!render::toImage(read.document, QStringLiteral("idle"), frame,
                                     {})
                         .isNull());
        }
        const qint64 elapsed = clock.elapsed();

        qInfo("rasterized Full HD Studio layers for %d frame(s) in %lld ms",
              frameCount, elapsed);
        QVERIFY2(elapsed < budgetMs,
                 qPrintable(QStringLiteral("initial raster took %1 ms; budget is %2 ms")
                                .arg(elapsed)
                                .arg(budgetMs)));
    }

    void fullHdSinglePixelEditStaysWithinBudget()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("full-hd-edit.json"));
        QVERIFY(Codec::writeFile(path, fullHdDocument(1)));

        DocumentModel document;
        QVERIFY(document.open(path));
        QElapsedTimer clock;
        clock.start();
        document.paint(1919, 1079, QStringLiteral("R"));
        const qint64 elapsed = clock.elapsed();

        QCOMPARE(document.slotAt(1919, 1079), QStringLiteral("R"));
        qInfo("edited the last pixel of a Full HD frame in %lld ms", elapsed);
        QVERIFY2(elapsed < 100,
                 qPrintable(QStringLiteral("single-pixel edit took %1 ms; budget is 100 ms")
                                .arg(elapsed)));
    }

    void addingAColourTouchesOneRow()
    {
        // The deterministic half of the stutter. Wall-clock on a loaded
        // machine swung by a factor of four between runs and proved nothing;
        // what the palette view is TOLD is exact. A reset makes it rebuild
        // every delegate, and holding a colour-adding key down does that
        // hundreds of times.
        DocumentModel doc;
        auto *rows = doc.paletteModel();
        QSignalSpy reset(rows, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(rows, &QAbstractItemModel::rowsInserted);
        QSignalSpy touched(rows, &QAbstractItemModel::dataChanged);

        const int before = rows->rowCount();
        doc.setPaletteColour(QStringLiteral("¢"), QStringLiteral("#123456"));
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(rows->rowCount(), before + 1);

        // Recolouring one slot touches one row and inserts nothing.
        doc.setPaletteColour(QStringLiteral("¢"), QStringLiteral("#654321"));
        QCOMPARE(touched.size(), 1);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(reset.size(), 0);

        // Painting is not a palette change at all, so the view hears nothing.
        doc.paint(1, 1, QStringLiteral("R"));
        QCOMPARE(touched.size(), 1);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(reset.size(), 0);

        // Opening another document is a genuine reset, and says so once.
        doc.reset(8, 8);
        QCOMPARE(reset.size(), 1);
    }

    void zoomDoesNotAllocateAPaintedBackingStore()
    {
        // QQuickPaintedItem scales its backing texture with the item. At 40x,
        // a Full HD canvas would ask the renderer for a 76800x43200 texture.
        QVERIFY(!PixelGridItem::staticMetaObject.inherits(
            &QQuickPaintedItem::staticMetaObject));
    }

    void repeatedPaintingDoesNotGetSlower()
    {
        // The studio stutters when russian roulette is held down. This drives
        // the real window so the cost of the QML side is in the measurement --
        // a C++-only benchmark would miss it entirely, and the QML side is
        // where the suspicion is.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        Theme theme;
        static InputLog hush(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(
            QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &hush);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(1100, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // A document with frames, because the timeline draws one thumbnail per
        // frame and they all repaint on any change.
        document.reset(64, 64);
        for (int i = 0; i < 11; ++i)
            document.addFrame(false);
        QCoreApplication::processEvents();

        QElapsedTimer clock;
        qint64 first = 0;
        qint64 last = 0;
        for (int round = 0; round < 6; ++round) {
            clock.restart();
            for (int i = 0; i < 10; ++i) {
                QTest::keyClick(window, Qt::Key_R);
                QCoreApplication::processEvents();
            }
            const qint64 took = clock.elapsed();
            if (round == 0)
                first = took;
            last = took;
        }
        qInfo("ten presses: %lld ms at the start, %lld ms after sixty", first, last);

        // The palette grows by one slot per press, and everything bound to it
        // is rebuilt on every change. If that is what costs, the last ten
        // presses take far longer than the first ten.
        // Deliberately loose: this machine's wall-clock swung by a factor of
        // four between runs of the same code. The tight guards on this are
        // `addingAColourTouchesOneRow` and the render test above, which measure
        // things that do not depend on how busy the machine is.
        QVERIFY2(last < first * 4 + 200,
                 "painting got dramatically slower as the palette grew");
    }

    // ---------------------------------------------------------- config

    void aSettingComesFromTheFileAndNothingElseIsInvented()
    {
        // The defaults are the program; the file is how you disagree with it.
        // A key nobody reads has to be reported, because a setting that does
        // nothing and says nothing is the failure a config file actually has.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[window]\n"
                   "hints = false\n"
                   "[canvas]\n"
                   "big_step = 4\n"
                   "grid = \"yes\"\n"
                   "wibble = 3\n");
        file.close();

        Config config;
        QCOMPARE(config.flag(QStringLiteral("window.hints")), false);
        QCOMPARE(config.number(QStringLiteral("canvas.big_step")), 4);
        // Untouched settings keep their defaults.
        QCOMPARE(config.number(QStringLiteral("window.width")), 1280);
        // A boolean written as a string is not read as one: "yes" is a string,
        // and a string is not false.
        QCOMPARE(config.flag(QStringLiteral("canvas.grid")), true);

        const QStringList problems = config.problems();
        QCOMPARE(problems.size(), 2);
        QVERIFY(problems.filter(QStringLiteral("canvas.grid")).size() == 1);
        QVERIFY(problems.filter(QStringLiteral("wibble")).size() == 1);
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void aKeyDoesWhatTheFileSaysItDoes()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[keys]\n"
                   "undo = \"alt+z\"\n"
                   "paint = [\"enter\", \"z\"]\n"
                   "roulette = \"\"\n"
                   "line_point = \"nonsense\"\n");
        file.close();

        Config config;
        QCOMPARE(config.action(Qt::Key_Z, Qt::AltModifier), QStringLiteral("undo"));
        // The old binding is gone, not added to.
        QVERIFY(config.action(Qt::Key_Z, Qt::ControlModifier).isEmpty());
        // Several keys for one action.
        QCOMPARE(config.action(Qt::Key_Return, Qt::NoModifier), QStringLiteral("paint"));
        QCOMPARE(config.action(Qt::Key_Z, Qt::NoModifier), QStringLiteral("paint"));
        // The keypad's Enter is the same key to anybody typing, and only one
        // of the two can be written in a file.
        QCOMPARE(config.action(Qt::Key_Enter, Qt::NoModifier), QStringLiteral("paint"));
        // An empty string gives the key back.
        QVERIFY(config.action(Qt::Key_R, Qt::NoModifier).isEmpty());
        // A binding that names no key is reported rather than ignored.
        QVERIFY(config.problems().filter(QStringLiteral("nonsense")).size() == 1);
        // And the untouched ones still work.
        QCOMPARE(config.action(Qt::Key_B, Qt::NoModifier), QStringLiteral("tool_pencil"));
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void punctuationIsBoundWithOrWithoutShift()
    {
        // `+` is shift-and-equals on one layout and a key of its own on
        // another. Somebody who wrote `zoom_in = "plus"` means the plus key on
        // both, so shift is forgiven -- but only when nothing matched exactly,
        // so a deliberate shift+comma still beats a plain comma.
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config config;
        QCOMPARE(config.action(Qt::Key_Plus, Qt::ShiftModifier), QStringLiteral("zoom_in"));
        QCOMPARE(config.action(Qt::Key_Plus, Qt::NoModifier), QStringLiteral("zoom_in"));
        QCOMPARE(config.action(Qt::Key_Comma, Qt::NoModifier),
                 QStringLiteral("frame_previous"));
        QCOMPARE(config.action(Qt::Key_Comma, Qt::ShiftModifier),
                 QStringLiteral("frame_move_back"));
        // Letters are never forgiven: c and shift+c are two commands here.
        QCOMPARE(config.action(Qt::Key_C, Qt::NoModifier), QStringLiteral("choose_colour"));
        QCOMPARE(config.action(Qt::Key_C, Qt::ShiftModifier),
                 QStringLiteral("replace_colour"));
    }

    void twoActionsOnOneKeyIsSaidOutLoud()
    {
        // The mistake a keybinding file makes, and the one whose symptom --
        // one of them silently never firing -- cannot be guessed at.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[keys]\ntool_hand = \"r\"\n");
        file.close();

        Config config;
        const QStringList clash = config.problems().filter(QStringLiteral("R is on both"));
        QCOMPARE(clash.size(), 1);
        QVERIFY(clash.first().contains(QStringLiteral("roulette")));
        QVERIFY(clash.first().contains(QStringLiteral("tool_hand")));
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void aBindingIsWrittenBothWaysRound()
    {
        // The menus need Qt's spelling and the hint bar needs a short one.
        // Both come from the same parsed binding, so a rebind cannot change
        // one and leave the other lying.
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config config;
        QCOMPARE(config.shortcut(QStringLiteral("save")), QStringLiteral("Ctrl+S"));
        QCOMPARE(config.label(QStringLiteral("save")), QStringLiteral("^S"));
        QCOMPARE(config.label(QStringLiteral("cancel")), QStringLiteral("Esc"));
        QCOMPARE(config.label(QStringLiteral("draw_mode")), QStringLiteral("d"));
        QCOMPARE(config.label(QStringLiteral("replace_colour")), QStringLiteral("⇧C"));
        QCOMPARE(config.label(QStringLiteral("slot_leader")), QStringLiteral(";"));
        // An action nobody bound says nothing rather than "".
        QVERIFY(config.label(QStringLiteral("toggle_hints")).isEmpty());
    }

    void savingTheFileRebindsTheKeysWhileTheWindowIsOpen()
    {
        // The claim the documentation makes. A keybinding file you have to
        // relaunch to try is a keybinding file that goes unedited, so the
        // watcher is part of the feature rather than a nicety.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());

        Config config;
        QCOMPARE(config.action(Qt::Key_B, Qt::NoModifier), QStringLiteral("tool_pencil"));
        QSignalSpy changed(&config, &Config::changed);

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[keys]\ntool_pencil = \"n\"\n");
        file.close();

        QVERIFY(changed.wait(4000));
        QCOMPARE(config.action(Qt::Key_N, Qt::NoModifier), QStringLiteral("tool_pencil"));
        QVERIFY(config.action(Qt::Key_B, Qt::NoModifier).isEmpty());
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void numericSettingsRejectValuesOutsideTheirDomain()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[window]\nwidth = -1\n"
                   "[canvas]\nzoom = 41\nbig_step = 0\n"
                   "[document]\nwidth = " + QByteArray::number(
                       Document::maxDimension + 1) + "\nfps = 0\n"
                   "[history]\ndepth = -5\n"
                   "[warnings]\nclips = -1\n");
        file.close();

        Config config;
        QCOMPARE(config.problems().size(), 7);
        QCOMPARE(config.number(QStringLiteral("window.width")), 1280);
        QCOMPARE(config.text(QStringLiteral("canvas.zoom")), QStringLiteral("fit"));
        QCOMPARE(config.number(QStringLiteral("warnings.clips")), 256);
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void caretMarginsAcceptNumbersOrCenterPerAxis()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());

        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[canvas]\ncaret_margin_x = \"center\"\n"
                       "caret_margin_y = 20\n");
            file.close();

            Config config;
            QVERIFY(config.problems().isEmpty());
            QCOMPARE(config.text(QStringLiteral("canvas.caret_margin_x")),
                     QStringLiteral("center"));
            QCOMPARE(config.number(QStringLiteral("canvas.caret_margin_y")), 20);
        }

        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("[canvas]\ncaret_margin_x = -1\n"
                       "caret_margin_y = \"edge\"\n");
            file.close();

            Config config;
            QCOMPARE(config.problems().size(), 2);
            QCOMPARE(config.number(QStringLiteral("canvas.caret_margin_x")), 0);
            QCOMPARE(config.number(QStringLiteral("canvas.caret_margin_y")), 0);
        }

        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
    }

    void newDocumentsUseTheConfiguredFps()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString path = home.path() + QStringLiteral("/config.toml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[document]\nfps = 24\n");
        file.close();

        qputenv("OMAPIXEL_CONFIG_PATH", path.toUtf8());
        Config::shared().load();
        DocumentModel document;
        QCOMPARE(document.fps(), 24);
        document.reset(16, 16);
        QCOMPARE(document.fps(), 24);

        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config::shared().load();
    }

    void studioScratchDefaultsOn()
    {
        // The default studio is addressable: an untitled window backs itself
        // with a runtime file an agent can draw into. The off switch in the
        // config file exists for whoever wants the old invisibility back.
        QCOMPARE(Config::shared().flag(QStringLiteral("studio.scratch")), true);
    }

    void largeDimensionsRoundTripThroughTheCodec()
    {
        // High-resolution grids -- a pixelized photo at SD size -- are a
        // legitimate document, not an abuse of the format. The ceiling is
        // Document::maxDimension, and this sits comfortably inside it.
        const QString path = QDir::temp().filePath(
            QStringLiteral("omapixel-large-roundtrip.json"));
        QVERIFY(Codec::writeFile(path, Document::blank(854, 480)));

        const Codec::Result read =
            Codec::readFile(path, Codec::WarningLimits());
        QVERIFY(read);
        QCOMPARE(read.document.columns(), 854);
        QCOMPARE(read.document.rows(), 480);
        QFile::remove(path);
    }

    void dimensionsPastTheCeilingAreRefused()
    {
        // The ceiling still exists: memory is the budget, and one over the
        // line fails with a sentence rather than an allocation death.
        QJsonObject size;
        size.insert(QStringLiteral("w"), Document::maxDimension + 1);
        size.insert(QStringLiteral("h"), 2);
        QJsonArray frames{QJsonArray{QStringLiteral("..")}};
        QJsonObject clip;
        clip.insert(QStringLiteral("name"), QStringLiteral("idle"));
        clip.insert(QStringLiteral("fps"), 8);
        clip.insert(QStringLiteral("frames"), frames);
        QJsonArray clips{clip};
        QJsonObject palette;
        QJsonObject root;
        root.insert(QStringLiteral("size"), size);
        root.insert(QStringLiteral("clips"), clips);
        root.insert(QStringLiteral("palette"), palette);

        const Codec::Result read = Codec::read(
            QJsonDocument(root).toJson(), Codec::WarningLimits());
        QVERIFY(!read);
        QVERIFY(read.error.contains(
            QString::number(Document::maxDimension)));
    }

    void theShippedConfigSaysWhatTheProgramActuallyDoes()
    {
        // config/config.toml is the documentation, the seed for `config write`
        // and what `--default-config` prints. If it drifts from the defaults
        // in Config.cpp it is worse than no file: it describes a program that
        // does not exist. So every commented line in it is checked against the
        // real default, and every default is checked for a line.
        QFile shipped(QStringLiteral(SOURCE_DIR "/config/config.toml"));
        QVERIFY2(shipped.open(QIODevice::ReadOnly), "config/config.toml is missing");
        const QString body = QString::fromUtf8(shipped.readAll());

        // Uncommenting every `# key = value` turns the shipped file into a
        // config that sets everything to its default -- which is exactly what
        // the parser should then agree with.
        QString uncommented;
        const QRegularExpression setting(
            QStringLiteral("^# ([a-z_]+ = .*)$"));
        QSet<QString> mentioned;
        QString section;
        const QStringList lines = body.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                section = line.mid(1, line.size() - 2);
                uncommented += line + QLatin1Char('\n');
                continue;
            }
            const auto match = setting.match(line);
            if (!match.hasMatch())
                continue;
            const QString assignment = match.captured(1);
            uncommented += assignment + QLatin1Char('\n');
            const QString name = assignment.section(QLatin1Char('='), 0, 0).trimmed();
            mentioned.insert(section.isEmpty() ? name
                                               : section + QLatin1Char('.') + name);
        }

        const toml::Table parsed = toml::read(uncommented);
        QVERIFY2(parsed.problems.isEmpty(),
                 qPrintable(parsed.problems.isEmpty()
                                ? QString()
                                : parsed.problems.first().message));

        for (const auto &defaulted : Config::settings()) {
            QVERIFY2(mentioned.contains(defaulted.first),
                     qPrintable(defaulted.first + QStringLiteral(" is not in config/config.toml")));
            QCOMPARE(parsed.value(defaulted.first).toString(),
                     defaulted.second.toString());
        }

        for (const auto &action : Config::actions()) {
            const QString key = QStringLiteral("keys.") + action.first;
            QVERIFY2(mentioned.contains(key),
                     qPrintable(key + QStringLiteral(" is not in config/config.toml")));
            // The default is stored as the file would write it, so the two can
            // be compared without either side knowing the other's syntax.
            QString wrote;
            const QVariant value = parsed.value(key);
            if (value.typeId() == QMetaType::QVariantList) {
                QStringList parts;
                for (const QVariant &one : value.toList())
                    parts << QLatin1Char('"') + one.toString() + QLatin1Char('"');
                wrote = QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
            } else {
                wrote = value.toString();
            }
            QCOMPARE(wrote, action.second);
        }

        // And nothing in the file that the program does not read.
        for (const QString &key : mentioned) {
            const bool known =
                std::any_of(Config::settings().begin(), Config::settings().end(),
                            [&key](const QPair<QString, QVariant> &s) { return s.first == key; })
                || std::any_of(Config::actions().begin(), Config::actions().end(),
                               [&key](const QPair<QString, QString> &a) {
                                   return QStringLiteral("keys.") + a.first == key;
                               });
            QVERIFY2(known, qPrintable(key + QStringLiteral(" in config/config.toml is read by nothing")));
        }
    }

    void nothingTheWindowSaysIsWrittenInTheQml()
    {
        // The catalogue test above checks that every key the QML asks for
        // exists. It cannot see a string that never asked -- and six of them
        // had quietly stayed in English through the whole i18n change, which
        // is exactly the failure a translator finds and nobody else does.
        QStringList literals;
        const QRegularExpression said(
            QStringLiteral("(?:doc\\.say|label:|text:)\\s*\"([A-Za-z][^\"]{3,})\""));
        QDirIterator qml(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                         {QStringLiteral("*.qml")}, QDir::Files);
        while (qml.hasNext()) {
            QFile file(qml.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString body = QString::fromUtf8(file.readAll());
            auto found = said.globalMatch(body);
            while (found.hasNext()) {
                const QString text = found.next().captured(1);
                // The program's own name is not a word in any language.
                if (text == QLatin1String("omapixel"))
                    continue;
                literals << text;
            }
        }
        QVERIFY2(literals.isEmpty(),
                 qPrintable(QStringLiteral("not in the catalogue: ")
                            + literals.join(QStringLiteral(" | "))));
    }

    void playingStopsOnTheLastFrameWhenTheLoopIsOff()
    {
        // A loop is right for judging movement and wrong for judging the last
        // frame -- which the loop keeps snatching away a twelfth of a second
        // after it arrives. Both are wanted, so it is a flag.
        qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");

        QQmlEngine engine;
        DocumentModel document;
        document.reset(8, 8);
        for (int i = 0; i < 3; ++i)
            document.addFrame(false);
        document.setFps(60);            // so the test does not sit and wait
        Theme theme;
        static InputLog mute(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &mute);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent main(&engine,
                           QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(main.isReady(), qPrintable(main.errorString()));
        QScopedPointer<QObject> root(main.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->resize(900, 640);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        const int last = document.frameCount() - 1;
        QCOMPARE(root->property("loop").toBool(), true);   // the default

        // Looping: from the last frame it comes back round, still playing.
        document.setFrame(last);
        QMetaObject::invokeMethod(root.data(), "togglePlay");
        QVERIFY(root->property("playing").toBool());
        QTRY_COMPARE(document.frame(), 0);
        QVERIFY(root->property("playing").toBool());
        QMetaObject::invokeMethod(root.data(), "togglePlay");
        QVERIFY(!root->property("playing").toBool());

        // Not looping: it stops, and it stops ON the last frame rather than
        // one past it, because the end of an animation is a thing you look at.
        root->setProperty("loop", false);
        document.setFrame(0);
        QMetaObject::invokeMethod(root.data(), "togglePlay");
        QTRY_VERIFY(!root->property("playing").toBool());
        QCOMPARE(document.frame(), last);

        // And pressing play again from there starts it over, rather than
        // doing nothing at all -- which reads as a broken button.
        QMetaObject::invokeMethod(root.data(), "togglePlay");
        QCOMPARE(document.frame(), 0);
        QVERIFY(root->property("playing").toBool());
    }

    void theWindowOnlyAsksForActionsThatExist()
    {
        // The same check the catalogue gets: every `cfg.shortcuts.x`,
        // `cfg.keys.x` and every action the key switch dispatches on has to be
        // an action Config knows. A typo here is a menu with no shortcut and a
        // key that does nothing, neither of which announces itself.
        QSet<QString> known;
        for (const auto &action : Config::actions())
            known.insert(action.first);

        QStringList invented;
        const QRegularExpression reference(
            QStringLiteral("cfg\\.(?:shortcuts|keys)\\.([a-z_]+)"));
        QDirIterator qml(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                         {QStringLiteral("*.qml")}, QDir::Files);
        while (qml.hasNext()) {
            QFile file(qml.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString body = QString::fromUtf8(file.readAll());
            auto found = reference.globalMatch(body);
            while (found.hasNext()) {
                const QString name = found.next().captured(1);
                if (!known.contains(name))
                    invented << name;
            }
        }
        QVERIFY2(invented.isEmpty(), qPrintable(invented.join(QStringLiteral(", "))));
    }

    // ------------------------------------------------------------ i18n

    void aCatalogueFallsBackRatherThanFailing()
    {
        // A half-translated catalogue should show English where it is thin,
        // and a typo in a key should be visible rather than a silent gap.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        // Asked for rather than assumed: the config directory carries the
        // application's name, and under the test runner that is not "omapixel".
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        const QString i18n =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/i18n");
        QDir().mkpath(i18n);

        // Written plainly rather than through a lambda with QVERIFY inside it:
        // the macro carries a `return` and a raw string full of commas, and the
        // two together confuse the preprocessor before the compiler ever runs.
        QFile base(i18n + QStringLiteral("/en.json"));
        QVERIFY(base.open(QIODevice::WriteOnly));
        base.write("{\"menu.file\": \"File\", \"menu.save\": \"Save\"}");
        base.close();

        QFile portuguese(i18n + QStringLiteral("/pt.json"));
        QVERIFY(portuguese.open(QIODevice::WriteOnly));
        portuguese.write("{\"menu.file\": \"Arquivo\"}");
        portuguese.close();

        Strings strings;
        strings.load(QStringLiteral("pt"));
        QCOMPARE(strings.language(), QStringLiteral("pt"));
        QCOMPARE(strings.t(QStringLiteral("menu.file")), QStringLiteral("Arquivo"));
        // Not translated yet: English, not a gap.
        QCOMPARE(strings.t(QStringLiteral("menu.save")), QStringLiteral("Save"));
        // Not a key at all: the key itself, which is visibly wrong.
        QCOMPARE(strings.t(QStringLiteral("menu.wibble")), QStringLiteral("menu.wibble"));

        // pt_BR with no catalogue of its own falls to pt, so a general
        // translation serves a specific system until somebody writes one.
        Strings brazilian;
        brazilian.load(QStringLiteral("pt_BR"));
        QCOMPARE(brazilian.language(), QStringLiteral("pt"));
        QCOMPARE(brazilian.t(QStringLiteral("menu.file")), QStringLiteral("Arquivo"));

        // A language nobody has written is English, not empty.
        Strings none;
        none.load(QStringLiteral("xx"));
        QCOMPARE(none.language(), QStringLiteral("en"));
        QCOMPARE(none.t(QStringLiteral("menu.file")), QStringLiteral("File"));
    }

    void theEnglishCatalogueCoversEveryKeyTheWindowAsksFor()
    {
        // The catalogue and the code drift apart the moment nothing checks
        // them. This is the check: every T.t("...") in the QML, and every
        // key the model says, has an English string.
        QFile english(QStringLiteral(SOURCE_DIR "/i18n/en.json"));
        QVERIFY2(english.open(QIODevice::ReadOnly), "i18n/en.json is missing");
        const QJsonObject catalogue =
            QJsonDocument::fromJson(english.readAll()).object();
        QVERIFY(catalogue.size() > 100);

        QStringList missing;
        const QRegularExpression call(QStringLiteral("T\\.t\\(([^)]*)\\)"));
        const QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
        QDirIterator qml(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                         {QStringLiteral("*.qml")}, QDir::Files);
        while (qml.hasNext()) {
            QFile file(qml.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString body = QString::fromUtf8(file.readAll());
            auto calls = call.globalMatch(body);
            while (calls.hasNext()) {
                auto keys = quoted.globalMatch(calls.next().captured(1));
                while (keys.hasNext()) {
                    const QString key = keys.next().captured(1);
                    if (!catalogue.contains(key))
                        missing << key;
                }
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("no English for: ") + missing.join(", ")));
    }

    void theRoundingComesFromHyprland()
    {
        // Whether an omarchy theme has round or square corners is Hyprland's
        // `decoration:rounding`, not anything in colors.toml. Parsed out of
        // `hyprctl -j getoption` here so it can be checked without one running.
        QCOMPARE(Theme::parseRounding(
                     R"({"option": "decoration:rounding", "int": 0, "set": true })"),
                 0);
        QCOMPARE(Theme::parseRounding(
                     R"({"option": "decoration:rounding", "int": 6, "set": true })"),
                 6);
        // Anything we do not recognise leaves the corners alone rather than
        // squaring them: no hyprctl, no compositor, a future output shape.
        QCOMPARE(Theme::parseRounding(""), -1);
        QCOMPARE(Theme::parseRounding("not json"), -1);
        QCOMPARE(Theme::parseRounding(R"({"option": "decoration:rounding"})"), -1);
        QCOMPARE(Theme::parseRounding(R"({"int": -3})"), -1);
    }

    void theCornersStartSquare()
    {
        QTemporaryDir state;
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        Theme theme;
        QCOMPARE(theme.rounding(), 0);
    }

    void aMissingThemeLeavesTheDefaults()
    {
        // A machine that is not running omarchy still has to open the studio.
        QTemporaryDir state;
        qputenv("XDG_STATE_HOME", state.path().toUtf8());
        Theme theme;
        QVERIFY(theme.background().isValid());
        QVERIFY(theme.accent().isValid());
    }

    void renderingAnUnknownClipIsEmptyAndNotACrash()
    {
        render::Options options;
        QVERIFY(render::toImage(sample(), QStringLiteral("nope"), 0, options).isNull());
        QVERIFY(render::toAnsi(sample(), QStringLiteral("nope"), 0).isEmpty());
    }
};

QTEST_MAIN(OmapixelTest)
#include "tst_omapixel.moc"
