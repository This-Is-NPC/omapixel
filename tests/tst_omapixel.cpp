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
