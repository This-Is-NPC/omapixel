// The core's tests.
//
// They cover the model and not the front ends, because the model is where the
// behaviour is: the CLI and the studio are argument parsing and pixels on
// screen over the same functions. A rule tested here is a rule both obey, which
// is the property the C++ rewrite exists to buy -- before, `resize` lived twice
// and only one copy had a test.

#include <QtTest>
#include <QCommandLineParser>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <qpa/qwindowsysteminterface.h>
#include <QProcess>
#include <QDir>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>

#include "Bridge.h"
#include "Codec.h"
#include "Document.h"
#include "Grid.h"
#include "Ops.h"
#include "Palette.h"
#include "Render.h"
#include "Commands.h"
#include "DocumentModel.h"
#include "InputLog.h"
#include "PixelGridItem.h"
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
        QVERIFY(info.output.contains(QStringLiteral("\"size\"")));
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
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("render")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("export")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("import")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("diff")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("new")));
        QVERIFY(!cli::isDocumentCommand(QStringLiteral("batch")));
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

        QQuickView view;
        // Surface.qml has no size of its own -- in the window it takes one from
        // the layout. Without this it loads at 0x0 and no pointer event can
        // reach it, which looks exactly like the bug being hunted.
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        DocumentModel document;
        document.reset(64, 64);
        Theme theme;

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
            }
        )", QUrl());
        QVERIFY2(stub.isReady(), qPrintable(stub.errorString()));
        QObject *win = stub.create();
        QVERIFY(win);

        view.rootContext()->setContextProperty(QStringLiteral("win"), win);
        view.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        view.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        static InputLog quiet(false);
        view.rootContext()->setContextProperty(QStringLiteral("log"), &quiet);

        QQmlComponent surfaceComponent(
            view.engine(),
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Surface.qml")));
        QVERIFY2(surfaceComponent.isReady(), qPrintable(surfaceComponent.errorString()));
        view.setContent(QUrl(), &surfaceComponent, surfaceComponent.create());
        view.resize(200, 200);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QQuickItem *surface = view.rootObject();
        QVERIFY(surface);

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
    }

    void aWheelOverTheWholeWindowReachesTheSurface()
    {
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

        // Tab means "give me the drawing", and brings the cursor out. Left to
        // Qt it walks the focus chain to somewhere with nothing to show for
        // it, which is how you end up not knowing where the focus is.
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(root->property("caretColumn").toInt(), 16);
        QCOMPARE(root->property("caretRow").toInt(), 12);
        root->setProperty("caretColumn", -1);
        root->setProperty("caretRow", -1);

        // First press puts the cursor in the middle rather than a corner.
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(root->property("caretColumn").toInt(), 16);
        QCOMPARE(root->property("caretRow").toInt(), 12);

        QTest::keyClick(window, Qt::Key_Right);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(root->property("caretColumn").toInt(), 17);
        QCOMPARE(root->property("caretRow").toInt(), 13);

        // Shift takes eight at a time.
        QTest::keyClick(window, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(root->property("caretColumn").toInt(), 9);

        // And it draws where it stands.
        QCOMPARE(document.slotAt(9, 13), QStringLiteral("."));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(document.slotAt(9, 13), root->property("slot").toString());

        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(document.slotAt(9, 13), QStringLiteral("."));

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
