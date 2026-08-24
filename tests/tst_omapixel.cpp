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
#include <algorithm>
#include <QJsonDocument>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#include "Bridge.h"
#include "Codec.h"
#include "Document.h"
#include "Differences.h"
#include "Grid.h"
#include "LayerOperations.h"
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
#include "Output.h"
#include "TextSafety.h"

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
    palette.insert(QStringLiteral("I"), QStringLiteral("#1A1B26FF"));
    palette.insert(QStringLiteral("S"), QStringLiteral("#F0CDBFFF"));
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

void registerQmlTypes()
{
    qmlRegisterType<PixelGridItem>("omapixel", 1, 0, "PixelGridItem");
    qmlRegisterUncreatableType<DocumentModel>(
        "omapixel", 1, 0, "DocumentModel", QStringLiteral("owned by the test harness"));
}

struct ProcessResult {
    int exitCode = -1;
    QByteArray output;
    QByteArray error;
};

ProcessResult runProcess(const QString &program, const QStringList &arguments,
                         const QProcessEnvironment &environment = {})
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    if (!environment.isEmpty())
        process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000))
        return {-1, process.readAllStandardOutput(), process.readAllStandardError()};
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

struct ScopedProcessCleanup
{
    QList<QProcess *> processes;

    ~ScopedProcessCleanup()
    {
        for (QProcess *process : processes) {
            if (!process || process->state() == QProcess::NotRunning)
                continue;
            process->terminate();
            if (!process->waitForFinished(2000)) {
                process->kill();
                process->waitForFinished(2000);
            }
        }
    }
};

struct StudioHarness
{
    DocumentModel document;
    Theme theme;
    InputLog log{false};
    QQmlEngine engine;
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    QString error;

    bool open(const QSize &size = QSize(1100, 800))
    {
        registerQmlTypes();
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &log);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        if (!component.isReady()) {
            error = component.errorString();
            return false;
        }
        root.reset(component.create());
        if (!root) {
            error = component.errorString();
            return false;
        }
        window = qobject_cast<QQuickWindow *>(root.data());
        if (!window) {
            error = QStringLiteral("Main.qml did not create a QQuickWindow");
            return false;
        }
        window->resize(size);
        window->show();
        if (!QTest::qWaitForWindowExposed(window)) {
            error = QStringLiteral("Studio window was not exposed");
            return false;
        }
        return true;
    }

    QObject *named(const QString &name) const
    {
        return window ? window->findChild<QObject *>(name) : nullptr;
    }

    bool invoke(const QString &commandId, const QVariantMap &args = {}) const
    {
        QObject *registry = named(QStringLiteral("commandRegistry"));
        QVariant executed;
        return registry
               && QMetaObject::invokeMethod(registry, "invoke",
                                            Q_RETURN_ARG(QVariant, executed),
                                            Q_ARG(QVariant, commandId),
                                            Q_ARG(QVariant, args))
               && executed.toBool();
    }
};

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
        QCOMPARE(doc.clip(QStringLiteral("idle"))->frameCount, 1);
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
        for (const Clip &clip : doc.clips())
            for (int frame = 0; frame < clip.frameCount; ++frame) {
                const Grid grid = doc.frame(clip.id, frame);
                QCOMPARE(grid.columns(), 6);
                QCOMPARE(grid.rows(), 5);
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
        QVERIFY(!doc.palette().set(Grid::Empty, QColor("#000000")));
        QVERIFY(doc.problems().isEmpty());
    }

    void paletteSlotValidationIsSharedByRuntimeAndCodec()
    {
        const QList<QChar> invalid{u'.', u'"', u'\\', QChar(0x00), QChar(0x1f),
                                   QChar(0x7f), QChar(0x80), QChar(0x9f)};
        for (const QChar slot : invalid) {
            QVERIFY(!Palette::validSlot(slot));
            Palette palette;
            QVERIFY(!palette.set(slot, QColor("#112233")));
            Document doc = Document::blank(1, 1);
            QString error;
            QVERIFY(!doc.setPaletteColour(slot, QColor("#112233"), &error));
            QVERIFY(error.startsWith(QStringLiteral("E_PALETTE_VALUE")));
        }
        QVERIFY(Palette::validSlot(QChar(0x20)));
         QVERIFY(!Palette::validSlot(QChar(0x2028)));
         QVERIFY(!Palette::validSlot(QChar(0x202E)));
         QVERIFY(text::isSafe(QStringLiteral("日本語")));
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

    void v2LayerIdentityLookupsAndMetadataRoundTrip()
    {
        Document doc = Document::blank(2, 2);
        const QString clipId = doc.clips().first().id;
        const QString layerId = doc.layers().first().id;
        QVERIFY(doc.addLayer(QStringLiteral("background"), QStringLiteral("Background"),
                             QStringLiteral("shared")));
        Layer *background = doc.layerById(QStringLiteral("background"));
        QVERIFY(background);
        background->visible = false;
        background->opacity = 192;
        background->mode = QStringLiteral("screen");
        QVERIFY(doc.setCel(QStringLiteral("background"), clipId, 0,
                           Grid::fromRows({QStringLiteral("AA"), QStringLiteral("A.")})));
        background->locked = true;

        QCOMPARE(doc.layerById(layerId)->name, QStringLiteral("Layer"));
        QCOMPARE(doc.layerByName(QStringLiteral("Background"))->id,
                 QStringLiteral("background"));
        QCOMPARE(doc.cel(QStringLiteral("Background"), clipId, 0).row(0),
                 QStringLiteral("AA"));
        const QString originalClipId = doc.clips().first().id;
        QVERIFY(doc.renameClip(QStringLiteral("idle"), QStringLiteral("Renamed")));
        QCOMPARE(doc.clips().first().id, originalClipId);
        QCOMPARE(doc.clipById(originalClipId)->name, QStringLiteral("Renamed"));

        const QByteArray encoded = Codec::write(doc);
        QCOMPARE(encoded, Codec::write(doc));
        const Codec::Result back = Codec::read(encoded);
        QVERIFY2(back.ok, qPrintable(back.error));
        QVERIFY(back.document == doc);
    }

    void layerIntegrityChecksCatchBrokenCelStorage()
    {
        Document doc = Document::blank(2, 2);
        doc.layers().first().cels.clear();
        QVERIFY(!doc.problems().isEmpty());
        QVERIFY(doc.problems().join(QStringLiteral("\n")).contains(
            QStringLiteral("invalid cel count")));
    }

    void layerEditsRespectScopeVisibilityAndLockState()
    {
        struct Case {
            QString storage;
            EditScope scope;
            bool hidden;
            bool locked;
        };
        const QList<Case> cases{
            {QStringLiteral("animated"), EditScope::CurrentFrame, false, false},
            {QStringLiteral("animated"), EditScope::AllFrames, true, false},
            {QStringLiteral("shared"), EditScope::CurrentFrame, false, false},
            {QStringLiteral("shared"), EditScope::AllFrames, true, false},
            {QStringLiteral("animated"), EditScope::CurrentFrame, true, true},
            {QStringLiteral("animated"), EditScope::AllFrames, false, true},
            {QStringLiteral("shared"), EditScope::CurrentFrame, true, true},
            {QStringLiteral("shared"), EditScope::AllFrames, false, true},
        };
        for (const Case &test : cases) {
            Document doc = Document::blank(2, 1);
            QVERIFY(doc.addClip(QStringLiteral("walk")));
            QVERIFY(doc.addFrame(QStringLiteral("idle"), 0, false));
            QVERIFY(doc.addLayer(QStringLiteral("target"), QStringLiteral("Target"),
                                 test.storage));
            Layer *layer = doc.layerById(QStringLiteral("target"));
            QVERIFY(layer);
            layer->visible = !test.hidden;
            QVERIFY(doc.setCel(QStringLiteral("target"), QStringLiteral("idle"), 0,
                               Grid::fromRows({QStringLiteral("I.")})));
            QVERIFY(doc.setCel(QStringLiteral("target"), QStringLiteral("idle"), 1,
                               Grid::fromRows({QStringLiteral("I.")})));
            layer->locked = test.locked;

            const Document before = doc;
            int changed = 0;
            QString error;
            const bool edited = doc.editLayer(
                QStringLiteral("target"), QStringLiteral("idle"), 0, test.scope,
                [](Grid &grid) { grid.set(1, 0, u'A'); }, &changed, &error);
            if (test.locked) {
                QVERIFY(!edited);
                QCOMPARE(doc, before);
                QVERIFY(error.startsWith(QStringLiteral("E_LAYER_LOCKED")));
                continue;
            }
            QVERIFY(edited);
            QCOMPARE(changed, test.storage == QStringLiteral("shared")
                                 ? 1
                                 : test.scope == EditScope::AllFrames ? 2 : 1);
            QCOMPARE(doc.cel(QStringLiteral("target"), QStringLiteral("idle"), 0)
                         .at(1, 0),
                     QChar(u'A'));
            QCOMPARE(doc.cel(QStringLiteral("target"), QStringLiteral("idle"), 1)
                         .at(1, 0),
                     test.storage == QStringLiteral("shared")
                         ? QChar(u'A')
                         : test.scope == EditScope::AllFrames ? QChar(u'A') : Grid::Empty);
        }
    }

    void frameStructureIsAtomicAndPreservesSharedCels()
    {
        Document doc = Document::blank(2, 1);
        QVERIFY(doc.addLayer(QStringLiteral("shared"), QStringLiteral("Shared"),
                             QStringLiteral("shared")));
        QVERIFY(doc.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral("I.")})));
        QVERIFY(doc.setCel(QStringLiteral("shared"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral(".A")})));
        const QList<Cel> sharedBefore = doc.layerById(QStringLiteral("shared"))->cels;
        QVERIFY(doc.addFrame(QStringLiteral("idle"), 0, true));
        QCOMPARE(doc.layerById(QStringLiteral("shared"))->cels, sharedBefore);
        QCOMPARE(doc.layerById(QStringLiteral("layer"))->cels.size(), 2);
        QCOMPARE(doc.cel(QStringLiteral("layer"), QStringLiteral("idle"), 1).row(0),
                 QStringLiteral("I."));
        QCOMPARE(doc.cel(QStringLiteral("shared"), QStringLiteral("idle"), 1).row(0),
                 QStringLiteral(".A"));

        Document malformed = doc;
        malformed.layerById(QStringLiteral("layer"))->cels.removeLast();
        const Document before = malformed;
        QVERIFY(!malformed.addFrame(QStringLiteral("idle"), 0, false));
        QCOMPARE(malformed, before);
    }

    void frameLimitsReturnDeterministicErrors()
    {
        Document frames = Document::blank(1, 1);
        for (int index = 1; index < Document::maxFramesPerClip; ++index)
            QVERIFY(frames.addFrame(QStringLiteral("idle"), index - 1, false));
        QString error;
        QVERIFY(!frames.addFrame(QStringLiteral("idle"), Document::maxFramesPerClip - 1,
                                 false, &error));
        QVERIFY(error.startsWith(QStringLiteral("E_FRAME_LIMIT")));

        Document cels = Document::blank(1, 1);
        for (int index = 1; index < Document::maxFramesPerClip - 1; ++index)
            QVERIFY(cels.addFrame(QStringLiteral("idle"), index - 1, false));
        for (int index = 0; index < 15; ++index)
            QVERIFY(cels.duplicateLayer(QStringLiteral("layer"),
                                        QStringLiteral("copy-%1").arg(index),
                                        QStringLiteral("Copy %1").arg(index)));
        QVERIFY(!cels.addFrame(QStringLiteral("idle"), 0, false, &error));
        QVERIFY(error.startsWith(QStringLiteral("E_CEL_LIMIT")));
    }

    void storageConversionReportsLossBeforeCollapse()
    {
        Document doc = Document::blank(2, 1);
        QVERIFY(doc.addFrame(QStringLiteral("idle"), 0, false));
        QVERIFY(doc.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral("I.")})));
        QVERIFY(doc.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 1,
                           Grid::fromRows({QStringLiteral(".I")})));
        int lost = 0;
        QString error;
        QVERIFY(!doc.convertLayerStorage(QStringLiteral("layer"), QStringLiteral("shared"),
                                          &lost, &error));
        QCOMPARE(lost, 2);
        QVERIFY(error.startsWith(QStringLiteral("E_LAYER_DATA_LOSS")));
        QCOMPARE(doc.layerById(QStringLiteral("layer"))->storage,
                 QStringLiteral("animated"));

        QVERIFY(doc.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 1,
                           Grid::fromRows({QStringLiteral("I.")})));
        QVERIFY(doc.convertLayerStorage(QStringLiteral("layer"), QStringLiteral("shared"),
                                        &lost, &error));
        QCOMPARE(doc.layerById(QStringLiteral("layer"))->cels.size(), 1);
        QVERIFY(doc.convertLayerStorage(QStringLiteral("layer"), QStringLiteral("animated"),
                                        &lost, &error));
        QCOMPARE(doc.layerById(QStringLiteral("layer"))->cels.size(), 2);
        doc.layerById(QStringLiteral("layer"))->locked = true;
        QVERIFY(!doc.setLayerMode(QStringLiteral("layer"), QStringLiteral("multiply"),
                                  &error));
        QVERIFY(error.startsWith(QStringLiteral("E_LAYER_LOCKED")));
    }

    void paletteRemovalAndBoundsUseEveryLayer()
    {
        Document doc = Document::blank(4, 3);
        QVERIFY(doc.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay"),
                             QStringLiteral("shared")));
        Layer *overlay = doc.layerById(QStringLiteral("overlay"));
        QVERIFY(overlay);
        overlay->visible = false;
        QVERIFY(doc.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral("...."), QStringLiteral("...I"),
                                           QStringLiteral("....")})));
        QCOMPARE(doc.drawnBounds(QStringLiteral("idle"), 0), QRect(3, 1, 1, 1));
        QString error;
        QVERIFY(!doc.removePaletteSlot(u'I', &error));
        QVERIFY(error.startsWith(QStringLiteral("E_PALETTE_IN_USE")));
        int changed = 0;
        QVERIFY(doc.editLayer(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                              EditScope::AllFrames,
                              [](Grid &grid) { ops::clear(grid); }, &changed, &error));
        QVERIFY(doc.removePaletteSlot(u'I', &error));
    }

    void differencesNameLayerAndCel()
    {
        Document left = Document::blank(2, 1);
        QVERIFY(left.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay"),
                              QStringLiteral("shared")));
        Document right = left;
        QVERIFY(right.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                             Grid::fromRows({QStringLiteral("I.")})));
        const QStringList differences = documentDifferences(left, right);
        QVERIFY(differences.join(QStringLiteral("\n")).contains(QStringLiteral("overlay")));
        QVERIFY(differences.join(QStringLiteral("\n")).contains(QStringLiteral("frame 0")));
    }

    void theLegacyObjectShapeIsRejectedByTheCleanCut()
    {
        // v1 is deliberately not a production codec input after the clean cut.
        const QByteArray legacy = R"({
            "size": {"w": 2, "h": 2},
            "palette": {"I": "#1A1B26"},
            "clips": {"idle": {"fps": 6, "frames": [["II", "I."]]}}
        })";
        const Codec::Result read = Codec::read(legacy);
        QVERIFY(!read.ok);
        QVERIFY(read.error.contains(QLatin1String("size")));
    }

    void badJsonIsAMessageAndNotACrash()
    {
        const Codec::Result read = Codec::read(QByteArray("{not json"));
        QVERIFY(!read.ok);
        QVERIFY(!read.error.isEmpty());
    }

    void aMissingCanvasIsRefused()
    {
        const Codec::Result read = Codec::read(QByteArray(R"({"clips": []})"));
        QVERIFY(!read.ok);
        QVERIFY(read.error.contains(QLatin1String("version")));
    }

    void malformedDocumentsAreRefusedBeforeTheyCanBeNormalised()
    {
        const QByteArray valid = R"({
            "version": 2,
            "canvas": {"width": 1, "height": 1},
            "palette": [{"slot": "I", "colour": "#112233FF"}],
            "clips": [{"id": "idle", "name": "Idle", "fps": 8, "frameCount": 1}],
            "layers": [{"id": "layer", "name": "Layer", "visible": true,
                         "locked": false, "opacity": 255, "mode": "normal",
                         "storage": "shared", "cels": [{"scope": "all", "rows": ["I"]}]}]
        })";
        QVERIFY(Codec::read(valid).ok);

        QByteArray wrongType = valid;
        wrongType.replace("\"fps\": 8", "\"fps\": \"8\"");
        QVERIFY(Codec::read(wrongType).error.contains(QLatin1String(".fps")));

        QByteArray shortRow = valid;
        shortRow.replace("[\"I\"]", "[\"\"]");
        QVERIFY(Codec::read(shortRow).error.contains(QLatin1String("characters")));

        QByteArray unknownSlot = valid;
        unknownSlot.replace("[\"I\"]", "[\"Z\"]");
        QVERIFY(Codec::read(unknownSlot).error.contains(QLatin1String("undefined")));

        const QByteArray duplicate = R"({
            "version": 2, "version": 2,
            "canvas": {"width": 1, "height": 1},
            "palette": [{"slot": "I", "colour": "#112233FF"}],
            "clips": [{"id": "idle", "name": "Idle", "fps": 8, "frameCount": 1}],
            "layers": [{"id": "layer", "name": "Layer", "visible": true,
                         "locked": false, "opacity": 255, "mode": "normal",
                         "storage": "shared", "cels": [{"scope": "all", "rows": ["I"]}]}]
        })";
        QVERIFY(Codec::read(duplicate).error.contains(QLatin1String("duplicate")));
    }

    void unicodeFormatCharactersAreRejectedAcrossRuntimeAndBridge()
    {
        const QString bidi = QStringLiteral("safe") + QChar(0x202E)
            + QStringLiteral("name");
        QVERIFY(!text::isSafe(bidi));
        QCOMPARE(text::escapeForTerminal(bidi), QStringLiteral("safe\\u202ename"));
        QVERIFY(!Palette::validSlot(QChar(0x202E)));

        Document document = sample();
        QString error;
        QVERIFY(!document.renameLayer(QStringLiteral("layer"), bidi, &error));
        QVERIFY(error.contains(QStringLiteral("invalid")));

        QJsonObject invalid = catalog();
        QJsonObject species = invalid.value(QStringLiteral("species")).toObject();
        species.insert(bidi, species.take(QStringLiteral("critter")));
        invalid.insert(QStringLiteral("species"), species);
        const Bridge::Result result = Bridge::importSpecies(
            invalid, bidi, QStringLiteral("0"));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("unsafe")));
    }

    void supplementaryUnicodeScalarsAreClassified()
    {
        const auto scalar = [](uint codepoint) {
            QString value;
            value += QChar::highSurrogate(codepoint);
            value += QChar::lowSurrogate(codepoint);
            return value;
        };
        QVERIFY(!text::isSafe(scalar(0xE0001)));
        QVERIFY(!text::isSafe(scalar(0x1FFFE)));
        QVERIFY(text::isSafe(scalar(0x1F600)));
        QVERIFY(text::isUnsafe(0xE0001));
        QVERIFY(text::isUnsafe(0x1FFFE));
        QVERIFY(!text::isUnsafe(0x1F600));
    }

    void duplicateScannerIsNullSafeForDuplicateAndMalformedJson()
    {
        QVERIFY(Codec::rejectDuplicateJsonKeys(
            QByteArrayLiteral(R"({"key":1,"key":2})"), nullptr));
        QVERIFY(Codec::rejectDuplicateJsonKeys(
            QByteArrayLiteral(R"({"key":})"), nullptr));
        QVERIFY(!Codec::rejectDuplicateJsonKeys(
            QByteArrayLiteral(R"({"key":1})"), nullptr));
    }

    void catalogDuplicateKeysAreRejectedBeforeQtNormalization()
    {
        const QByteArray duplicate =
            R"({"palette":{},"palette":{},"states":{},"species":{}})";
        QString error;
        QVERIFY(Codec::rejectDuplicateJsonKeys(duplicate, &error));
        QVERIFY(error.contains(QStringLiteral("duplicate")));
        QVERIFY(QJsonDocument::fromJson(duplicate).object().contains(
            QStringLiteral("palette")));
    }

    void cliCatalogImportAndExportRejectDuplicateKeys()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString catalogPath = root.filePath(QStringLiteral("catalog.json"));
        QFile catalogFile(catalogPath);
        QVERIFY(catalogFile.open(QIODevice::WriteOnly));
        catalogFile.write(R"({"palette":{},"palette":{},"states":{},"species":{}})");
        catalogFile.close();
        const QString documentPath = root.filePath(QStringLiteral("document.json"));
        QVERIFY(writeDocument(documentPath, sample()));
        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

        ProcessResult result = runProcess(
            cli, {QStringLiteral("import"), catalogPath, QStringLiteral("--name"),
                  QStringLiteral("critter"), QStringLiteral("--out"),
                  root.filePath(QStringLiteral("imported.json"))}, environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("duplicate")));

        result = runProcess(
            cli, {QStringLiteral("export"), documentPath, catalogPath,
                  QStringLiteral("--name"), QStringLiteral("critter")}, environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("duplicate")));
    }

    void cliRejectsNonRegularDocumentCatalogAndBatchInputs()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString fifoPath = root.filePath(QStringLiteral("input.fifo"));
        QVERIFY(::mkfifo(fifoPath.toLocal8Bit().constData(), 0600) == 0);
        const QString documentPath = root.filePath(QStringLiteral("document.json"));
        QVERIFY(writeDocument(documentPath, sample()));
        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

        ProcessResult result = runProcess(
            cli, {QStringLiteral("show"), fifoPath}, environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("input is not a regular file")));

        result = runProcess(
            cli, {QStringLiteral("batch"), documentPath, QStringLiteral("--script"),
                  fifoPath}, environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("input is not a regular file")));

        result = runProcess(
            cli, {QStringLiteral("import"), fifoPath, QStringLiteral("--name"),
                  QStringLiteral("critter"), QStringLiteral("--out"),
                  root.filePath(QStringLiteral("imported.json"))}, environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("input is not a regular file")));
    }

    void boundedReaderUsesOneDescriptorForSwapsSymlinksAndFifos()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath(QStringLiteral("input.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("1234"), qint64(4));
        file.close();

        QByteArray bytes;
        QString error;
        QVERIFY(input::readRegularFile(path, 4, &bytes, &error));
        QCOMPARE(bytes, QByteArrayLiteral("1234"));
        QVERIFY(input::readRegularFile(path, 3, &bytes, &error));
        QCOMPARE(bytes.size(), 4);

        const QString link = root.filePath(QStringLiteral("input-link.json"));
        QVERIFY(QFile::link(path, link));
        QVERIFY(!input::readRegularFile(link, 4, &bytes, &error));
        QVERIFY(error.contains(QStringLiteral("regular file")));

        QVERIFY(QFile::remove(path));
        QVERIFY(::mkfifo(path.toLocal8Bit().constData(), 0600) == 0);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!input::readRegularFile(path, 4, &bytes, &error));
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(error.contains(QStringLiteral("regular file")));
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

    void terminalOutputBudgetRefusesBeforeBuildingOutput()
    {
        const Document huge = Document::blank(2048, 2048);
        QStringList diagnostics;
        QVERIFY(render::toText(huge, QStringLiteral("idle"), 0, {}, &diagnostics).isEmpty());
        QVERIFY(diagnostics.first().contains(QStringLiteral("hard cell limit")));
        diagnostics.clear();
        QVERIFY(render::toAnsi(huge, QStringLiteral("idle"), 0, {}, &diagnostics).isEmpty());
        QVERIFY(diagnostics.first().contains(QStringLiteral("hard cell limit")));
    }

    void compositeLayersMatchGoldenPixelsAndIsolation()
    {
        Document doc = Document::blank(2, 2);
        doc.palette() = Palette();
        doc.palette().set(u'R', QColor(220, 40, 50, 255));
        doc.palette().set(u'G', QColor(30, 190, 80, 255));
        doc.palette().set(u'B', QColor(40, 70, 220, 255));
        QVERIFY(doc.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral("R."),
                                           QStringLiteral(".R")})));
        QVERIFY(doc.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay"),
                             QStringLiteral("shared")));
        QVERIFY(doc.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral(".G"),
                                           QStringLiteral("B.")})));

        const QImage image = render::toImage(doc, QStringLiteral("idle"), 0, {});
        QCOMPARE(image.size(), QSize(2, 2));
        QCOMPARE(QColor::fromRgba(image.pixel(0, 0)), QColor(220, 40, 50, 255));
        QCOMPARE(QColor::fromRgba(image.pixel(1, 0)), QColor(30, 190, 80, 255));
        QCOMPARE(QColor::fromRgba(image.pixel(0, 1)), QColor(40, 70, 220, 255));
        QCOMPARE(QColor::fromRgba(image.pixel(1, 1)), QColor(220, 40, 50, 255));

        doc.layerById(QStringLiteral("overlay"))->visible = false;
        const QImage hidden = render::toImage(doc, QStringLiteral("idle"), 0, {});
        QCOMPARE(QColor::fromRgba(hidden.pixel(1, 0)), QColor(0, 0, 0, 0));
        QCOMPARE(QColor::fromRgba(hidden.pixel(0, 0)), QColor(220, 40, 50, 255));
        doc.layerById(QStringLiteral("overlay"))->visible = true;

        render::Options isolated;
        isolated.isolated = true;
        isolated.layer = QStringLiteral("layer");
        const QImage base = render::toImage(doc, QStringLiteral("idle"), 0, isolated);
        QCOMPARE(QColor::fromRgba(base.pixel(1, 0)), QColor(0, 0, 0, 0));
        isolated.layer = QStringLiteral("overlay");
        const QImage top = render::toImage(doc, QStringLiteral("idle"), 0, isolated);
        QCOMPARE(QColor::fromRgba(top.pixel(0, 0)), QColor(0, 0, 0, 0));
        QCOMPARE(QColor::fromRgba(top.pixel(1, 0)), QColor(30, 190, 80, 255));

        QCOMPARE(render::toText(doc, QStringLiteral("idle"), 0),
                 QStringLiteral("RG\nBR\n"));
    }

    void sourceOverUsesContractedIntegerRoundingAtEveryOpacity()
    {
        const QList<int> opacities{0, 1, 50, 99, 100};
        const QList<QColor> expected{
            QColor(0, 0, 0, 0),
            QColor(255, 0, 0, 1),
            QColor(199, 102, 51, 50),
            QColor(201, 100, 49, 99),
            QColor(199, 99, 51, 100),
        };
        for (int index = 0; index < opacities.size(); ++index) {
            Document doc = Document::blank(1, 1);
            doc.palette() = Palette();
            doc.palette().set(u'S', QColor(200, 100, 50, 255));
            doc.setFrame(QStringLiteral("idle"), 0,
                         Grid::fromRows({QStringLiteral("S")}));
            doc.layers().first().opacity = opacities.at(index);
            const QImage image = render::toImage(doc, QStringLiteral("idle"), 0, {});
            QCOMPARE(QColor::fromRgba(image.pixel(0, 0)), expected.at(index));
        }
    }

    void sourceOverCompositesAlphaOverTheFinalChecker()
    {
        Document doc = Document::blank(1, 1);
        doc.palette() = Palette();
        doc.palette().set(u'R', QColor(255, 0, 0, 128));
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("R")}));
        render::Options options;
        options.checker = true;
        options.checkerDark = QColor(16, 16, 16);
        options.checkerLight = QColor(32, 32, 32);
        const QImage image = render::toImage(doc, QStringLiteral("idle"), 0, options);
        QCOMPARE(QColor::fromRgba(image.pixel(0, 0)), QColor(136, 8, 8, 255));
    }

    void textFlatteningUsesPaletteOrderForEqualDistances()
    {
        Document doc = Document::blank(1, 1);
        doc.palette() = Palette();
        doc.palette().set(u'F', QColor(10, 10, 10, 128));
        doc.palette().set(u'S', QColor(10, 10, 12, 128));
        doc.setFrame(QStringLiteral("idle"), 0,
                     Grid::fromRows({QStringLiteral("F")}));
        QVERIFY(doc.addLayer(QStringLiteral("top"), QStringLiteral("Top"),
                             QStringLiteral("shared")));
        QVERIFY(doc.setCel(QStringLiteral("top"), QStringLiteral("idle"), 0,
                           Grid::fromRows({QStringLiteral("S")})));
        QCOMPARE(render::toText(doc, QStringLiteral("idle"), 0),
                 QStringLiteral("F\n"));
    }

    void quantizerGoldenUsesPaletteOrderAndReportsLoss()
    {
        Document doc = Document::blank(1, 1);
        doc.palette() = Palette();
        doc.palette().set(u'A', QColor(0, 0, 0));
        doc.palette().set(u'B', QColor(254, 254, 254));
        doc.setFrame(QStringLiteral("idle"), 0, Grid::fromRows({QStringLiteral("B")}));
        doc.layers().first().opacity = 127;
        doc.layers().first().cels.first().grid = Grid::fromRows({QStringLiteral("A")});

        render::QuantizationReport report;
        const Grid flattened = render::toGrid(doc, QStringLiteral("idle"), 0,
                                              render::Options(), nullptr, &report);
        QCOMPARE(flattened.toRows(), QStringList{QStringLiteral("A")});
        QCOMPARE(report.composedPixels, 1);
        QCOMPARE(report.exactMatches, 0);
        QCOMPARE(report.approximatedPixels, 1);
        QCOMPARE(report.newSlots, 0);
    }

    void mergeDownPreviewAndApplyHaveIdenticalConsequences()
    {
        Document source = Document::blank(2, 1);
        source.palette() = Palette();
        source.palette().set(u'A', QColor(255, 0, 0));
        source.palette().set(u'B', QColor(0, 0, 255));
        source.palette().set(u'C', QColor(128, 0, 127));
        source.setFrame(QStringLiteral("idle"), 0,
                        Grid::fromRows({QStringLiteral("B.")}));
        QVERIFY(source.addLayer(QStringLiteral("top"), QStringLiteral("Top"),
                                QStringLiteral("shared")));
        Layer *top = source.layerById(QStringLiteral("top"));
        top->opacity = 128;
        QVERIFY(source.setCel(QStringLiteral("top"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("A.")})));

        const QByteArray before = Codec::write(source);
        const LayerOperationResult preview = previewMergeDown(source, QStringLiteral("top"));
        QVERIFY2(preview, qPrintable(preview.error));
        QCOMPARE(Codec::write(source), before);
        QCOMPARE(preview.report.frames, 1);
        QCOMPARE(preview.report.affectedPixels, 1);
        QCOMPARE(preview.report.exactMatches, 1);
        QCOMPARE(preview.report.approximatedPixels, 0);
        QCOMPARE(preview.report.newSlots, 0);
        QCOMPARE(preview.document.layers().size(), 1);
        QCOMPARE(preview.document.layers().first().cels.first().grid.toRows(),
                 QStringList{QStringLiteral("C.")});

        Document applied = source;
        LayerOperationReport appliedReport;
        QString error;
        QVERIFY2(applyMergeDown(&applied, QStringLiteral("top"), &appliedReport, &error),
                 qPrintable(error));
        QCOMPARE(appliedReport.frames, preview.report.frames);
        QCOMPARE(appliedReport.affectedPixels, preview.report.affectedPixels);
        QCOMPARE(appliedReport.exactMatches, preview.report.exactMatches);
        QCOMPARE(appliedReport.approximatedPixels, preview.report.approximatedPixels);
        QCOMPARE(Codec::write(applied), Codec::write(preview.document));
    }

    void mergeDownHonoursVisibilityOpacityOrderAndLocks()
    {
        Document doc = Document::blank(1, 1);
        QVERIFY(doc.addLayer(QStringLiteral("hidden"), QStringLiteral("Hidden"),
                             QStringLiteral("shared")));
        Layer *hidden = doc.layerById(QStringLiteral("hidden"));
        hidden->visible = false;
        const QByteArray beforeHidden = Codec::write(doc);
        const LayerOperationResult hiddenResult = previewMergeDown(doc, QStringLiteral("hidden"));
        QVERIFY(!hiddenResult);
        QVERIFY(hiddenResult.error.startsWith(QStringLiteral("E_LAYER_VISIBILITY")));
        QCOMPARE(Codec::write(doc), beforeHidden);

        hidden->visible = true;
        doc.layers().first().visible = false;
        const LayerOperationResult hiddenTarget =
            previewMergeDown(doc, QStringLiteral("hidden"));
        QVERIFY(!hiddenTarget);
        QVERIFY(hiddenTarget.error.startsWith(QStringLiteral("E_LAYER_VISIBILITY")));
        doc.layers().first().visible = true;
        doc.layers().first().locked = true;
        const QByteArray beforeLocked = Codec::write(doc);
        const LayerOperationResult lockedResult = previewMergeDown(doc, QStringLiteral("hidden"));
        QVERIFY(!lockedResult);
        QVERIFY(lockedResult.error.startsWith(QStringLiteral("E_LAYER_LOCKED")));
        QCOMPARE(Codec::write(doc), beforeLocked);

        doc.layers().first().locked = false;
        hidden->locked = true;
        const LayerOperationResult sourceLocked = previewMergeDown(doc, QStringLiteral("hidden"));
        QVERIFY(!sourceLocked);
        QVERIFY(sourceLocked.error.startsWith(QStringLiteral("E_LAYER_LOCKED")));
    }

    void flattenVisibleIsSeparateDeterministicAndCancelSafe()
    {
        Document source = Document::blank(2, 1);
        source.palette() = Palette();
        source.palette().set(u'A', QColor(255, 0, 0));
        source.palette().set(u'B', QColor(0, 0, 255));
        source.setFrame(QStringLiteral("idle"), 0,
                        Grid::fromRows({QStringLiteral("A.")}));
        QVERIFY(source.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay"),
                                QStringLiteral("shared")));
        QVERIFY(source.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral(".B")})));
        QVERIFY(source.addLayer(QStringLiteral("guide"), QStringLiteral("Guide"),
                                QStringLiteral("shared")));
        source.layerById(QStringLiteral("guide"))->visible = false;
        QVERIFY(source.setCel(QStringLiteral("guide"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("BB")})));

        const QByteArray before = Codec::write(source);
        const LayerOperationResult preview = previewFlattenVisible(source);
        QVERIFY2(preview, qPrintable(preview.error));
        QCOMPARE(Codec::write(source), before);
        QCOMPARE(preview.document.layers().size(), 1);
        QCOMPARE(preview.document.layers().first().name, QStringLiteral("Flattened"));
        QCOMPARE(preview.document.layers().first().cels.first().grid.toRows(),
                 QStringList{QStringLiteral("AB")});
        QCOMPARE(preview.report.affectedPixels, 2);
        QCOMPARE(preview.report.exactMatches, 2);
        QCOMPARE(preview.report.approximatedPixels, 0);
        QCOMPARE(preview.report.newSlots, 0);

        const QByteArray firstOutput = Codec::write(preview.document);
        const LayerOperationResult repeated = flattenVisible(preview.document);
        QVERIFY2(repeated, qPrintable(repeated.error));
        QCOMPARE(Codec::write(repeated.document), firstOutput);

        Document applied = source;
        LayerOperationReport appliedReport;
        QString error;
        QVERIFY2(applyFlattenVisible(&applied, &appliedReport, &error), qPrintable(error));
        QCOMPARE(Codec::write(applied), firstOutput);
        QCOMPARE(appliedReport.frames, preview.report.frames);
        QCOMPARE(appliedReport.affectedPixels, preview.report.affectedPixels);
        QCOMPARE(appliedReport.exactMatches, preview.report.exactMatches);
        QCOMPARE(Codec::write(source), before);
    }

    void unknownSlotsAreSkippedAndDiagnosedOnEverySurface()
    {
        Document doc = Document::blank(1, 1);
        doc.layers().first().cels.first().grid =
            Grid::fromRows({QStringLiteral("?")});
        QStringList diagnostics;
        render::Options options;
        const QImage image = render::toImage(doc, QStringLiteral("idle"), 0,
                                              options, nullptr, nullptr,
                                              &diagnostics);
        QCOMPARE(QColor::fromRgba(image.pixel(0, 0)), QColor(0, 0, 0, 0));
        QCOMPARE(diagnostics, QStringList{QStringLiteral("unknown palette slot `?` skipped")});
        diagnostics.clear();
        QCOMPARE(render::toText(doc, QStringLiteral("idle"), 0, options, &diagnostics),
                 QStringLiteral(".\n"));
        QCOMPARE(diagnostics, QStringList{QStringLiteral("unknown palette slot `?` skipped")});
    }

    void compositeCliCanSelectAnIsolatedLayer()
    {
        Document doc = Document::blank(1, 1);
        doc.palette() = Palette();
        doc.palette().set(u'A', QColor(255, 0, 0));
        doc.palette().set(u'B', QColor(0, 0, 255));
        doc.setFrame(QStringLiteral("idle"), 0, Grid::fromRows({QStringLiteral("A")}));
        QVERIFY(doc.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay"),
                             QStringLiteral("shared")));
        doc.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                   Grid::fromRows({QStringLiteral("B")}));
        QCOMPARE(run(doc, QStringLiteral("text --isolated --layer-id overlay")).output,
                 QStringLiteral("B\n"));
    }

    void nativeLayersCrossSurfaceAcceptanceFixture()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString runtime = root.path() + QStringLiteral("/runtime");
        QVERIFY(QDir().mkpath(runtime));
        QVERIFY(QFile::setPermissions(runtime, QFileDevice::ReadOwner
                                               | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner));
        const bool hadPreviousRuntime = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", runtime.toUtf8());

        const QString documentPath = root.path() + QStringLiteral("/acceptance.json");
        QVERIFY(QFile::copy(QStringLiteral(SOURCE_DIR
                                           "/tests/fixtures/format-v2/valid/animated-shared.json"),
                            documentPath));
        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const QProcessEnvironment environment = [] {
            QProcessEnvironment value = QProcessEnvironment::systemEnvironment();
            value.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
            value.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
            return value;
        }();

        ProcessResult result = runProcess(
            cli, {QStringLiteral("layer"), documentPath, QStringLiteral("list")}, environment);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(result.output.contains("\"hero\""));

        result = runProcess(cli, {QStringLiteral("paint"), documentPath,
                                  QStringLiteral("--layer-id"), QStringLiteral("hero"),
                                  QStringLiteral("--scope"), QStringLiteral("frame"),
                                  QStringLiteral("--frame"), QStringLiteral("1"),
                                  QStringLiteral("--at"), QStringLiteral("2,1"),
                                  QStringLiteral("--slot"), QStringLiteral("A")}, environment);
        QCOMPARE(result.exitCode, 0);

        DocumentModel studio;
        QVERIFY(studio.open(documentPath));
        studio.setActiveLayerId(QStringLiteral("hero"));
        studio.setFrame(0);
        const QString beforeUndo = studio.slotAt(0, 0);
        studio.beginStroke();
        studio.paint(0, 0, QStringLiteral("B"));
        studio.endStroke();
        QCOMPARE(studio.slotAt(0, 0), QStringLiteral("B"));
        studio.undo();
        QCOMPARE(studio.slotAt(0, 0), beforeUndo);
        studio.beginStroke();
        studio.paint(2, 0, QStringLiteral("A"));
        studio.endStroke();
        QVERIFY(studio.dirty());
        QVERIFY(studio.save());
        QVERIFY(!studio.dirty());
        // A reload of the just-saved bytes is deliberately a no-op; the later
        // CLI write below proves the adopting path and returns true.
        QVERIFY(!studio.reloadFromDisk());
        QCOMPARE(studio.slotAt(2, 0), QStringLiteral("A"));

         SessionPublisher publisher;
         publisher.follow(&studio);
         QJsonObject published = QJsonDocument::fromJson(publisher.snapshot()).object();
         QVERIFY(!published.isEmpty());
         QCOMPARE(published.value(QStringLiteral("path")).toString(),
                  QFileInfo(documentPath).absoluteFilePath());
         QCOMPARE(published.value(QStringLiteral("view")).toObject()
                      .value(QStringLiteral("layerId")).toString(), QStringLiteral("hero"));

        result = runProcess(cli, {QStringLiteral("paint"), documentPath,
                                  QStringLiteral("--layer-id"), QStringLiteral("hero"),
                                  QStringLiteral("--scope"), QStringLiteral("frame"),
                                  QStringLiteral("--frame"), QStringLiteral("0"),
                                  QStringLiteral("--at"), QStringLiteral("0,0"),
                                  QStringLiteral("--slot"), QStringLiteral("A")}, environment);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(studio.reloadFromDisk());
        QCOMPARE(studio.slotAt(0, 0), QStringLiteral("A"));

        const QString composite = root.path() + QStringLiteral("/composite.png");
        const QString isolated = root.path() + QStringLiteral("/isolated.png");
        result = runProcess(cli, {QStringLiteral("render"), documentPath,
                                  QStringLiteral("-o"), composite}, environment);
        QCOMPARE(result.exitCode, 0);
        result = runProcess(cli, {QStringLiteral("render"), documentPath,
                                  QStringLiteral("-o"), isolated, QStringLiteral("--isolated"),
                                  QStringLiteral("--layer-id"), QStringLiteral("hero")}, environment);
        QCOMPARE(result.exitCode, 0);
        QVERIFY(QFileInfo::exists(composite));
        QVERIFY(QFileInfo::exists(isolated));
        QFile compositeFile(composite);
        QFile isolatedFile(isolated);
        QVERIFY(compositeFile.open(QIODevice::ReadOnly));
        QVERIFY(isolatedFile.open(QIODevice::ReadOnly));
        QVERIFY(compositeFile.readAll() != isolatedFile.readAll());

        const QString flattened = root.path() + QStringLiteral("/flattened.json");
        result = runProcess(cli, {QStringLiteral("flatten"), documentPath,
                                  QStringLiteral("-o"), flattened}, environment);
        QCOMPARE(result.exitCode, 1);
        result = runProcess(cli, {QStringLiteral("flatten"), documentPath,
                                  QStringLiteral("-o"), flattened, QStringLiteral("--anyway")},
                            environment);
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.error.contains(QByteArrayLiteral("E_LAYER_LOCKED")));
        QVERIFY(!QFileInfo::exists(flattened));
        publisher.retire();
        if (hadPreviousRuntime)
            qputenv("XDG_RUNTIME_DIR", previousRuntime);
        else
            qunsetenv("XDG_RUNTIME_DIR");
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

    void bridgeImportUsesTheFullV2PaletteSlotRules()
    {
        QJsonObject invalid = catalog();
        QJsonObject palette = invalid.value(QStringLiteral("palette")).toObject();
        palette.insert(QStringLiteral("."), QStringLiteral("#11223344"));
        invalid.insert(QStringLiteral("palette"), palette);
        Bridge::Result result = Bridge::importSpecies(
            invalid, QStringLiteral("critter"), QStringLiteral("0"));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("invalid slot")));

        invalid = catalog();
        palette = invalid.value(QStringLiteral("palette")).toObject();
        palette.insert(QStringLiteral("X"), QStringLiteral("#112233"));
        invalid.insert(QStringLiteral("palette"), palette);
        result = Bridge::importSpecies(invalid, QStringLiteral("critter"), QStringLiteral("0"));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("#RRGGBBAA")));

        invalid = catalog();
        invalid.insert(QStringLiteral("palette"), QJsonArray());
        result = Bridge::importSpecies(invalid, QStringLiteral("critter"), QStringLiteral("0"));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("must be an object")));
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
        Layer *layer = doc.layer(doc.layerIds().first());
        layer->cels[0].grid = Grid::fromRows({QStringLiteral("RRRRRRRRRRRR"),
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
        const cli::Outcome painted = run(doc, QStringLiteral("paint --at 2,3 --slot R --frame 0"));
        QCOMPARE(painted.code, 0);
        QVERIFY(painted.changed);
        QCOMPARE(doc.frame(doc.clipNames().value(0), 0).at(2, 3), QChar(u'R'));
    }

    void cliDrawingTargetsLayerScopeAndLockPolicy()
    {
        Document doc = Document::blank(2, 1);
        QVERIFY(doc.addFrame(QStringLiteral("idle"), 0, false));
        QVERIFY(doc.addLayer(QStringLiteral("shared"), QStringLiteral("Shared"),
                             QStringLiteral("shared")));

        QCOMPARE(run(doc, QStringLiteral(
                         "paint --layer-id layer --frame 0 --at 0,0 --slot I"))
                     .code,
                 0);
        QCOMPARE(run(doc, QStringLiteral(
                         "paint --layer-id layer --all-frames --at 1,0 --slot I"))
                     .code,
                 0);
        QCOMPARE(doc.cel(QStringLiteral("layer"), QStringLiteral("idle"), 0).at(1, 0),
                 QChar(u'I'));
        QCOMPARE(doc.cel(QStringLiteral("layer"), QStringLiteral("idle"), 1).at(1, 0),
                 QChar(u'I'));

        const cli::Outcome shared = run(
            doc, QStringLiteral("paint --layer-id shared --at 0,0 --slot I"));
        QCOMPARE(shared.code, 0);
        QCOMPARE(doc.cel(QStringLiteral("shared"), QStringLiteral("idle"), 1).at(0, 0),
                 QChar(u'I'));

        doc.layerById(QStringLiteral("layer"))->locked = true;
        const cli::Outcome locked = run(
            doc, QStringLiteral("paint --layer-id layer --frame 0 --at 0,0 --slot A"));
        QCOMPARE(locked.code, 1);
        QVERIFY(locked.error.contains(QStringLiteral("E_LAYER_LOCKED")));
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
                      QStringLiteral("layer[0] (layer/layer, Layer/Layer) cel idle frame 0: dimensions are "
                                     "4x3 in before, 6x5 in after"),
                      QStringLiteral("clip[0] (idle/idle): FPS is 8 in before, 12 in after")}));

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
                     QStringLiteral("layer[0] (layer/layer, Layer/Layer) cel idle frame 0: 4 pixel(s) differ")}));

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
        QCOMPARE(doc.clip(QStringLiteral("walk"))->frameCount, 2);
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
                          "layer[0] (layer/layer, Layer/Layer) cel idle frame 0: 4 pixel(s) differ"));
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
                      + QStringLiteral("layer[0] (layer/layer, Layer/Layer) cel idle frame 0: 9 pixel(s) differ")
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

    void adversarialRenderInputsAreRejectedBeforeAllocation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.json"));
        const QString output = dir.filePath(QStringLiteral("render.png"));
        QVERIFY(writeDocument(source, sample()));
        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const ProcessResult malformed = runProcess(
            cli, {QStringLiteral("render"), source, QStringLiteral("--frame"),
                  QStringLiteral("wat"), QStringLiteral("-o"), output});
        QCOMPARE(malformed.exitCode, 2);
        QVERIFY(malformed.error.contains("E_FRAME_OUT_OF_RANGE"));
        const ProcessResult outOfRange = runProcess(
            cli, {QStringLiteral("render"), source, QStringLiteral("--frame"),
                  QStringLiteral("99"), QStringLiteral("-o"), output});
        QCOMPARE(outOfRange.exitCode, 2);
        QVERIFY(outOfRange.error.contains("E_FRAME_OUT_OF_RANGE"));

        Document huge = Document::blank(Document::maxDimension, Document::maxDimension);
        render::Options options;
        options.scale = 64;
        options.sheet = true;
        QString error;
        const QImage image = render::toImage(huge, QStringLiteral("idle"), 0,
                                             options, nullptr, &error);
        QVERIFY(image.isNull());
        QVERIFY(error.contains(QStringLiteral("hard budget")));
    }

    void derivedOutputsRejectSymlinksAndSourceAliases()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.json"));
        const QString alias = dir.filePath(QStringLiteral("alias.json"));
        const QString dangling = dir.filePath(QStringLiteral("dangling.json"));
        QVERIFY(writeDocument(source, sample()));
        QVERIFY(QFile::link(source, alias));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("missing.json")), dangling));
        const QByteArray before = Codec::write(sample());
        QString error;
        QVERIFY(!output::validate(alias, {source}, &error));
        QVERIFY(error.contains(QStringLiteral("symlink")));
        error.clear();
        QVERIFY(!output::validate(dangling, {}, &error));
        QVERIFY(error.contains(QStringLiteral("symlink")));

        DocumentModel model;
        QVERIFY(model.open(source));
        QVERIFY(!model.exportImage(alias, 1, false, false));
        const Codec::Result preserved = Codec::readFile(source);
        QVERIFY(preserved);
        QCOMPARE(Codec::write(preserved.document), before);

        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const ProcessResult flattened = runProcess(
            cli, {QStringLiteral("flatten"), source, QStringLiteral("-o"), alias,
                  QStringLiteral("--anyway")});
        QCOMPARE(flattened.exitCode, 2);
        QVERIFY(flattened.error.contains("E_FLATTEN_OUTPUT"));
        const Codec::Result afterFlatten = Codec::readFile(source);
        QVERIFY(afterFlatten);
        QCOMPARE(Codec::write(afterFlatten.document), before);
    }

    void flattenAnywayOnlyConfirmsPaletteLossAndNeverLocks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("locked.json"));
        const QString output = dir.filePath(QStringLiteral("flattened.json"));
        Document locked = sample();
        locked.layerById(QStringLiteral("layer"))->locked = true;
        QVERIFY(writeDocument(source, locked));

        DocumentModel model;
        QVERIFY(model.open(source));
        QVERIFY(!model.flatten());
        QVERIFY(model.note().contains(QStringLiteral("locked")));

        const QString cli = qEnvironmentVariable(
            "OMAPIXEL_CLI", QStringLiteral(SOURCE_DIR "/build/bin/omapixel"));
        const ProcessResult refused = runProcess(
            cli, {QStringLiteral("flatten"), source, QStringLiteral("-o"), output,
                  QStringLiteral("--anyway")});
        QCOMPARE(refused.exitCode, 1);
        QVERIFY(refused.error.contains(QByteArrayLiteral("E_LAYER_LOCKED")));
        QVERIFY(!QFile::exists(output));
    }

    void oversizedInMemoryDocumentsNeverReplaceTheirDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString output = dir.filePath(QStringLiteral("destination.json"));
        QFile destination(output);
        QVERIFY(destination.open(QIODevice::WriteOnly));
        destination.write("untouched");
        destination.close();

        Document huge = Document::blank(Document::maxDimension, Document::maxDimension);
        for (int i = 0; i < 4; ++i)
            QVERIFY(huge.addLayer(QStringLiteral("extra-%1").arg(i),
                                  QStringLiteral("Extra %1").arg(i),
                                  QStringLiteral("shared")));
        QString error;
        QVERIFY(Codec::write(huge, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("serialized document")));
        QVERIFY(!Codec::writeFile(output, huge, &error));
        QFile verify(output);
        QVERIFY(verify.open(QIODevice::ReadOnly));
        QCOMPARE(verify.readAll(), QByteArray("untouched"));
    }

    void codecBoundsRejectTinyAmplificationAndPathologicalInput()
    {
        const QByteArray tiny = R"({"version":2,"canvas":{"width":1,"height":1},
"palette":[],"clips":[{"id":"idle","name":"Idle","fps":8,"frameCount":999999}],
"layers":[]})";
        const Codec::Result excessive = Codec::read(tiny);
        QVERIFY(!excessive);
        QVERIFY(excessive.error.contains(QStringLiteral("frameCount")));

        QJsonArray clips;
        clips.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("idle")},
                                 {QStringLiteral("name"), QStringLiteral("Idle")},
                                 {QStringLiteral("fps"), 8},
                                 {QStringLiteral("frameCount"), 300}});
        QJsonArray layers;
        for (int i = 0; i < 55; ++i) {
            layers.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("layer-%1").arg(i)},
                                      {QStringLiteral("name"), QStringLiteral("Layer %1").arg(i)},
                                      {QStringLiteral("visible"), true},
                                      {QStringLiteral("locked"), false},
                                      {QStringLiteral("opacity"), 255},
                                      {QStringLiteral("mode"), QStringLiteral("normal")},
                                      {QStringLiteral("storage"), QStringLiteral("animated")},
                                      {QStringLiteral("cels"), QJsonArray()}});
        }
        const QJsonObject amplified{{QStringLiteral("version"), 2},
                                    {QStringLiteral("canvas"), QJsonObject{{QStringLiteral("width"), 1},
                                                                              {QStringLiteral("height"), 1}}},
                                    {QStringLiteral("palette"), QJsonArray()},
                                    {QStringLiteral("clips"), clips},
                                    {QStringLiteral("layers"), layers}};
        const Codec::Result tooManyCels =
            Codec::read(QJsonDocument(amplified).toJson(QJsonDocument::Compact));
        QVERIFY(!tooManyCels);
        QVERIFY(tooManyCels.error.contains(QStringLiteral("cel count")));

        QByteArray deep;
        deep.fill('[', 70);
        deep += '0';
        deep.append(QByteArray(70, ']'));
        const Codec::Result nested = Codec::read(deep);
        QVERIFY(!nested);
        QVERIFY(nested.error.contains(QStringLiteral("nesting depth")));

        const QByteArray oversized(Document::maxDocumentBytes + 1, ' ');
        const Codec::Result tooLarge = Codec::read(oversized);
        QVERIFY(!tooLarge);
        QVERIFY(tooLarge.error.contains(QStringLiteral("hard limit")));

        QByteArray control = Codec::write(sample());
        control.replace("Layer", "Bad\\nName");
        const Codec::Result unsafeName = Codec::read(control);
        QVERIFY(!unsafeName);
        QVERIFY(unsafeName.error.contains(QStringLiteral("control")));
    }

    void clipboardBoundsAreCheckedBeforeMaterialization()
    {
        DocumentModel model;
        QGuiApplication::clipboard()->setText(
            QString(Document::maxClipboardBytes + 1, QLatin1Char('x')));
        QVERIFY(!model.pastePixels(0, 0));
        QVERIFY(model.note().contains(QStringLiteral("clipboard")));

        QJsonArray row;
        for (int i = 0; i < Document::maxClipboardColumns + 1; ++i)
            row.append(QJsonValue::Null);
        QJsonArray rows;
        rows.append(row);
        QGuiApplication::clipboard()->setText(
            QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact)));
        QVERIFY(!model.pastePixels(0, 0));
    }

    void lockedStructuralActionsUseTheCorePolicy()
    {
        DocumentModel model;
        QVERIFY(model.addLayer(QStringLiteral("locked"), QStringLiteral("Locked")));
        QVERIFY(model.setLayerLocked(QStringLiteral("locked"), true));
        const QList<Layer> before = model.document().layers();
        QVERIFY(!model.moveLayers({QStringLiteral("locked")}, 0));
        QVERIFY(!model.removeLayers({QStringLiteral("locked")}));
        QCOMPARE(model.document().layers(), before);
        QVERIFY(model.note().contains(QStringLiteral("locked")));
    }

    void activeLayerChangedIsPublishedOncePerDerivedIdentityChange()
    {
        DocumentModel model;
        QVERIFY(model.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        QSignalSpy changed(&model, &DocumentModel::activeLayerChanged);

        model.setActiveLayerId(QStringLiteral("overlay"));
        QCOMPARE(changed.count(), 1);
        changed.clear();
        QVERIFY(model.removeLayer(QStringLiteral("overlay")));
        QCOMPARE(changed.count(), 1);

        QVERIFY(model.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        model.setActiveLayerId(QStringLiteral("overlay"));
        changed.clear();
        QVERIFY(model.mergeDown(QStringLiteral("overlay")));
        QCOMPARE(changed.count(), 1);

        QVERIFY(model.addLayer(QStringLiteral("top"), QStringLiteral("Top")));
        model.setActiveLayerId(QStringLiteral("top"));
        changed.clear();
        QVERIFY(model.flatten());
        QCOMPARE(changed.count(), 1);
    }

    void fakeSessionWithLivePidAndWrongExecutableIsPruned()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        const QString path = sessions::directory() + QStringLiteral("/%1.json")
                                                     .arg(QCoreApplication::applicationPid());
        QJsonObject body{
            {QStringLiteral("version"), 2},
            {QStringLiteral("pid"), QCoreApplication::applicationPid()},
            {QStringLiteral("started"), double(sessions::startTimeOf(QCoreApplication::applicationPid()))},
            {QStringLiteral("executable"), QStringLiteral("/tmp/fake-studio")},
            {QStringLiteral("path"), QString()},
            {QStringLiteral("dirty"), false},
            {QStringLiteral("view"), QJsonObject{{QStringLiteral("clip"), QStringLiteral("idle")},
                                                   {QStringLiteral("frame"), 0},
                                                   {QStringLiteral("layerId"), QStringLiteral("layer")},
                                                   {QStringLiteral("layerName"), QStringLiteral("Layer")},
                                                   {QStringLiteral("scope"), QStringLiteral("frame")}}},
            {QStringLiteral("selection"), QJsonValue::Null}};
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(body).toJson());
        file.close();
        QVERIFY(sessions::live().isEmpty());
        QVERIFY(!QFile::exists(path));
    }

    void sessionPublicationFailuresAreVisible()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        DocumentModel model;
        const QString target = runtime.filePath(QStringLiteral("target.json"));
        QFile targetFile(target);
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.write("untouched");
        targetFile.close();
        QString ipcError;
        const int occupied = sessions::openPublisher(QCoreApplication::applicationPid(),
                                                     &ipcError);
        QVERIFY2(occupied >= 0, qPrintable(ipcError));
        SessionPublisher publisher;
        QSignalSpy failure(&publisher, &SessionPublisher::publicationFailed);
        publisher.follow(&model);
        QVERIFY(failure.count() >= 1);
        ::close(occupied);
        QFile verify(target);
        QVERIFY(verify.open(QIODevice::ReadOnly));
        QCOMPARE(verify.readAll(), QByteArray("untouched"));
    }

    void modelRoutesEveryRasterToolThroughTheActiveLayerAndScope()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("layers.json"));
        Document source = Document::blank(4, 2);
        QVERIFY(source.addFrame(QStringLiteral("idle"), 0, false));
        QVERIFY(source.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        QVERIFY(source.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("IIII"), QStringLiteral("....")})));
        QVERIFY(source.setCel(QStringLiteral("overlay"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("...."), QStringLiteral("....")})));
        QVERIFY(writeDocument(path, source));

        DocumentModel model;
        QVERIFY(model.open(path));
        model.setActiveLayerId(QStringLiteral("overlay"));
        QCOMPARE(model.activeLayerId(), QStringLiteral("overlay"));
        QCOMPARE(model.activeLayerName(), QStringLiteral("Overlay"));
        const Grid baseBefore = model.document().cel(QStringLiteral("layer"),
                                                      QStringLiteral("idle"), 0);

        model.paint(0, 1, QStringLiteral("R"));
        model.line(0, 1, 1, 1, QStringLiteral("R"));
        model.rect(2, 0, 3, 1, QStringLiteral("A"), false);
        model.fill(2, 0, QStringLiteral("B"));
        model.shift(1, 0);
        model.flip(QStringLiteral("x"));
        model.clearFrame();
        QCOMPARE(model.document().cel(QStringLiteral("layer"), QStringLiteral("idle"), 0),
                 baseBefore);
        QCOMPARE(model.slotAt(0, 0), QStringLiteral("."));

        model.setEditScope(QStringLiteral("all-frames"));
        model.paint(3, 1, QStringLiteral("R"));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0)
                     .at(3, 1),
                 QChar(u'R'));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 1)
                     .at(3, 1),
                 QChar(u'R'));

        QGuiApplication::clipboard()->setText(QStringLiteral("[[\"#F7768E\"]]"));
        QVERIFY(model.pastePixels(0, 0));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0)
                     .at(0, 0),
                 QChar(u'R'));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 1)
                     .at(0, 0),
                 QChar(u'R'));
        QCOMPARE(model.document().cel(QStringLiteral("layer"), QStringLiteral("idle"), 1)
                     .at(0, 0),
                 Grid::Empty);
    }

    void sharedLayersExposeOneCelAndPickerHasExplicitScopes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("shared.json"));
        Document source = Document::blank(3, 1);
        QVERIFY(source.addFrame(QStringLiteral("idle"), 0, false));
        QVERIFY(source.addLayer(QStringLiteral("shared"), QStringLiteral("Shared"),
                                QStringLiteral("shared")));
        QVERIFY(source.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("I..")})));
        QVERIFY(source.setCel(QStringLiteral("layer"), QStringLiteral("idle"), 1,
                              Grid::fromRows({QStringLiteral("I..")})));
        QVERIFY(source.setCel(QStringLiteral("shared"), QStringLiteral("idle"), 0,
                              Grid::fromRows({QStringLiteral("..R")})));
        QVERIFY(writeDocument(path, source));

        DocumentModel model;
        QVERIFY(model.open(path));
        model.setActiveLayerId(QStringLiteral("shared"));
        model.setFrame(1);
        model.paint(1, 0, QStringLiteral("A"));
        QCOMPARE(model.document().cel(QStringLiteral("shared"), QStringLiteral("idle"), 0)
                     .at(1, 0),
                 QChar(u'A'));
        QCOMPARE(model.document().cel(QStringLiteral("shared"), QStringLiteral("idle"), 1)
                     .at(1, 0),
                 QChar(u'A'));

        model.setActiveLayerId(QStringLiteral("shared"));
        QCOMPARE(model.pickSlot(0, 0, false), QStringLiteral("."));
        QCOMPARE(model.pickSlot(0, 0, true), QStringLiteral("I"));
        model.setPickerScope(QStringLiteral("composite"));
        QCOMPARE(model.pickerScope(), QStringLiteral("composite"));
        QCOMPARE(model.layers().at(1).toMap().value(QStringLiteral("shared")).toBool(), true);
    }

    void lockedActiveLayerRefusesEditsWithLocalizedNote()
    {
        DocumentModel model;
        QVERIFY(model.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        model.setActiveLayerId(QStringLiteral("overlay"));
        QVERIFY(model.setLayerLocked(QStringLiteral("overlay"), true));
        const Grid before = model.document().cel(QStringLiteral("overlay"),
                                                 QStringLiteral("idle"), 0);
        model.paint(0, 0, QStringLiteral("R"));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0),
                 before);
        QCOMPARE(model.note(), QStringLiteral("layer Overlay is locked"));
        QVERIFY(model.activeLayerLocked());
    }

    void activeLayerIdentitySurvivesOpenReloadAndUsesDeterministicFallback()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("identity.json"));
        Document first = Document::blank(2, 1);
        QVERIFY(first.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        QVERIFY(writeDocument(path, first));

        DocumentModel model;
        QVERIFY(model.open(path));
        model.setActiveLayerId(QStringLiteral("overlay"));

        Document renamed = first;
        QVERIFY(renamed.renameLayer(QStringLiteral("overlay"), QStringLiteral("Renamed")));
        QVERIFY(writeDocument(path, renamed));
        QVERIFY(model.reloadFromDisk());
        QCOMPARE(model.activeLayerId(), QStringLiteral("overlay"));
        QCOMPARE(model.activeLayerName(), QStringLiteral("Renamed"));

        Document withoutOverlay = Document::blank(2, 1);
        QVERIFY(writeDocument(path, withoutOverlay));
        QVERIFY(model.reloadFromDisk());
        QCOMPARE(model.activeLayerId(), QStringLiteral("layer"));
    }

    void layerStructureUndoRestoresActiveIdentityAndContent()
    {
        DocumentModel model;
        QVERIFY(model.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        model.setActiveLayerId(QStringLiteral("overlay"));
        model.paint(1, 1, QStringLiteral("R"));
        QVERIFY(model.removeLayer(QStringLiteral("overlay")));
        QCOMPARE(model.activeLayerId(), QStringLiteral("layer"));
        model.undo();
        QCOMPARE(model.activeLayerId(), QStringLiteral("overlay"));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0)
                     .at(1, 1),
                 QChar(u'R'));
        model.undo();
        QVERIFY(model.document().layerById(QStringLiteral("overlay")));
        model.undo();
        QCOMPARE(model.document().layerById(QStringLiteral("overlay")), nullptr);
    }

    void modelExposesDockOperationsAndConsequenceReports()
    {
        DocumentModel model;
        QVERIFY(model.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        QVERIFY(model.addLayer(QStringLiteral("shared"), QStringLiteral("Shared"),
                               QStringLiteral("shared")));
        QCOMPARE(model.layers().size(), 3);
        QCOMPARE(model.layers().at(2).toMap().value(QStringLiteral("animated")).toBool(),
                 false);

        model.setActiveLayerId(QStringLiteral("overlay"));
        model.setEditScope(QStringLiteral("all-frames"));
        model.paint(0, 0, QStringLiteral("I"));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0)
                     .at(0, 0),
                 QChar(u'I'));
        QVERIFY(model.clearLayer(QStringLiteral("overlay"), true));
        QCOMPARE(model.document().cel(QStringLiteral("overlay"), QStringLiteral("idle"), 0)
                     .at(0, 0),
                 Grid::Empty);

        QVERIFY(model.setLayersVisible({QStringLiteral("layer"), QStringLiteral("overlay")},
                                       false));
        QVERIFY(!model.document().layerById(QStringLiteral("layer"))->visible);
        QVERIFY(model.setLayersLocked({QStringLiteral("layer"), QStringLiteral("overlay")},
                                      true));
        QVERIFY(model.document().layerById(QStringLiteral("overlay"))->locked);
         QVERIFY(!model.moveLayers({QStringLiteral("overlay")}, 2));
         QVERIFY(model.setLayersLocked({QStringLiteral("overlay")}, false));
         QVERIFY(model.moveLayers({QStringLiteral("overlay")}, 2));
        QCOMPARE(model.document().layers().at(2).id, QStringLiteral("overlay"));
        QVERIFY(model.removeLayers({QStringLiteral("overlay"), QStringLiteral("shared")}));
        QCOMPARE(model.document().layers().size(), 1);

        DocumentModel conversion;
        QVERIFY(conversion.addLayer(QStringLiteral("animated"), QStringLiteral("Animated")));
        conversion.setActiveLayerId(QStringLiteral("animated"));
        conversion.setFrame(0);
        conversion.paint(0, 0, QStringLiteral("I"));
        QVERIFY(conversion.frameCount() == 1);
        const QVariantMap storage = conversion.layerStoragePreview(
            QStringLiteral("animated"), QStringLiteral("shared"));
        QVERIFY(storage.value(QStringLiteral("ok")).toBool());
        QVERIFY(conversion.setLayerStorage(QStringLiteral("animated"),
                                           QStringLiteral("shared"), false));
        QCOMPARE(conversion.activeLayerStorage(), QStringLiteral("shared"));

        const QVariantMap merge = conversion.mergeDownPreview(QStringLiteral("shared"));
        QVERIFY(!merge.value(QStringLiteral("ok")).toBool());

        DocumentModel flatten;
        QVERIFY(flatten.addLayer(QStringLiteral("top"), QStringLiteral("Top")));
        const QVariantMap flat = flatten.flattenPreview();
        QVERIFY(flat.value(QStringLiteral("ok")).toBool());
        QVERIFY(flatten.flatten());
        QCOMPARE(flatten.layers().size(), 1);
    }

    void layerDockIsAKeyboardLayerBrowser()
    {
        QTest::failOnWarning();
        registerQmlTypes();
        DocumentModel document;
        QVERIFY(document.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        Theme theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());

        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/LayerDock.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setProperty("width", 260);
        QCoreApplication::processEvents();

        auto *list = root->findChild<QQuickItem *>(QStringLiteral("layerList"));
        QVERIFY(list);
        QCOMPARE(list->width(), 260.0);
        QVERIFY(root->findChild<QQuickItem *>(QStringLiteral("structuralSelectionBanner")));
        QVERIFY(!root->findChild<QQuickItem *>(QStringLiteral("selectedLayerSection")));
        QVERIFY(!root->findChild<QQuickItem *>(QStringLiteral("opacitySlider")));

        QSignalSpy activated(root.data(), SIGNAL(layerActivated(QString)));
        QSignalSpy requested(root.data(), SIGNAL(commandRequested(QString,QVariant)));
        QVERIFY(QMetaObject::invokeMethod(root.data(), "toggleStructural",
                                          Q_ARG(QVariant, QStringLiteral("overlay"))));
        const QVariantList selected = root->property("selectedIds").toList();
        QVERIFY(selected.contains(QStringLiteral("layer")));
        QVERIFY(selected.contains(QStringLiteral("overlay")));

        QVERIFY(QMetaObject::invokeMethod(root.data(), "focusList"));
        QVERIFY(QMetaObject::invokeMethod(list, "step", Q_ARG(QVariant, 1)));
        QVERIFY(QMetaObject::invokeMethod(list, "activateCurrent"));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().at(0).toString(), QStringLiteral("layers.select"));
        QCOMPARE(requested.first().at(1).toMap().value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("overlay"));
        QCOMPARE(document.activeLayerId(), QStringLiteral("layer"));

        QVERIFY(QMetaObject::invokeMethod(root.data(), "activate",
                                          Q_ARG(QVariant, QStringLiteral("overlay"))));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("overlay"));
        QCOMPARE(document.activeLayerId(), QStringLiteral("overlay"));

        QQuickItem *row = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex",
                                          Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 1)));
        QVERIFY(row);
        QVERIFY(row->property("activePaintTarget").toBool());
        QVERIFY(row->findChild<QQuickItem *>(QStringLiteral("visibilityAction_overlay")) == nullptr);
        QVERIFY(row->findChild<QQuickItem *>(QStringLiteral("lockAction_overlay")) == nullptr);

        root->setProperty("width", 220);
        QCoreApplication::processEvents();
        QCOMPARE(list->width(), 220.0);
    }

    void layerToolIsTopLevelWithIndependentGeometryAndRetainedActions()
    {
        QTest::failOnWarning();
        registerQmlTypes();
        DocumentModel document;
        document.reset(32, 24);
        Theme theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        static InputLog silent(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &silent);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY(root);
        auto *studio = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(studio);
        studio->resize(900, 700);
        studio->show();
        QVERIFY(QTest::qWaitForWindowExposed(studio));
        QCoreApplication::processEvents();

        auto *dock = studio->findChild<QQuickItem *>(QStringLiteral("layerDock"));
        auto *tool = studio->findChild<QQuickWindow *>(QStringLiteral("layerToolWindow"));
        auto *panel = studio->findChild<QQuickItem *>(QStringLiteral("dockPanel"));
        auto *stage = studio->findChild<QQuickItem *>(QStringLiteral("stage"));
        QVERIFY(dock);
        QVERIFY(tool);
        QVERIFY(panel);
        QVERIFY(stage);
        QVERIFY(tool->type() == Qt::Window);
        QVERIFY(tool->flags().testFlag(Qt::Window));
        QCOMPARE(tool->transientParent(), studio);
        QCOMPARE(tool->modality(), Qt::NonModal);
        QVERIFY(!tool->isVisible());

        QVERIFY(QMetaObject::invokeMethod(dock, "activate",
                                          Q_ARG(QVariant, QStringLiteral("layer"))));
        QTRY_VERIFY_WITH_TIMEOUT(tool->isVisible(), 1000);
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolVisibilityAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolLockAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolRenameField")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolOpacitySlider")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolNormalModeButton")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolDuplicateAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolMoveUpAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolMoveDownAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolDeleteAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolClearFrameAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolClearAllAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolConvertSharedAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolMergeAction")));
        QVERIFY(tool->findChild<QQuickItem *>(QStringLiteral("toolFlattenAction")));

        const int independentX = studio->x() + studio->width() + 33;
        const int independentY = studio->y() + 41;
        tool->setX(independentX);
        tool->setY(independentY);
        tool->resize(420, 640);
        QCoreApplication::processEvents();
        QCOMPARE(tool->x(), independentX);
        QCOMPARE(tool->y(), independentY);
        QCOMPARE(tool->width(), 420);
        QCOMPARE(tool->height(), 640);
        QVERIFY(stage->width() > 0);
        QCOMPARE(studio->findChild<QQuickItem *>(QStringLiteral("selectedLayerSection")), nullptr);

        QVERIFY(QMetaObject::invokeMethod(tool, "closeTool"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool->isVisible(), 1000);
        auto *list = studio->findChild<QQuickItem *>(QStringLiteral("layerList"));
        QVERIFY(list);
        QTRY_VERIFY_WITH_TIMEOUT(list->hasActiveFocus(), 1000);

        panel->setWidth(260);
        QCoreApplication::processEvents();
        QVERIFY(stage->width() > 0);
    }

    void layerToolKeyboardLifecycleAndConfirmationReturn()
    {
        QTest::failOnWarning();
        registerQmlTypes();
        DocumentModel document;
        document.reset(32, 24);
        QVERIFY(document.addLayer(QStringLiteral("overlay"), QStringLiteral("Overlay")));
        Theme theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        static InputLog silent(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &silent);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        auto *studio = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(studio);
        studio->resize(1000, 760);
        studio->show();
        QVERIFY(QTest::qWaitForWindowExposed(studio));

        auto *dock = studio->findChild<QQuickItem *>(QStringLiteral("layerDock"));
        auto *list = studio->findChild<QQuickItem *>(QStringLiteral("layerList"));
        auto *tool = studio->findChild<QQuickWindow *>(QStringLiteral("layerToolWindow"));
        auto *sheet = studio->findChild<QObject *>(QStringLiteral("layerSheet"));
        const auto activeVisible = [&document] {
            for (const QVariant &value : document.layers()) {
                const QVariantMap layer = value.toMap();
                if (layer.value(QStringLiteral("id")).toString() == document.activeLayerId())
                    return layer.value(QStringLiteral("visible")).toBool();
            }
            return false;
        };
        QVERIFY(dock);
        QVERIFY(list);
        QVERIFY(tool);
        QVERIFY(sheet);

        QVERIFY(QMetaObject::invokeMethod(dock, "focusList"));
        QTRY_VERIFY_WITH_TIMEOUT(list->hasActiveFocus(), 1000);
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        QTest::keyClick(studio, Qt::Key_Down);
        QCOMPARE(list->property("currentIndex").toInt(), 1);
        QTest::keyClick(studio, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(tool->isVisible(), 1000);
        QCOMPARE(document.activeLayerId(), QStringLiteral("overlay"));

        QVERIFY(QMetaObject::invokeMethod(tool, "focusWindow"));
        QTRY_VERIFY_WITH_TIMEOUT(tool->activeFocusItem(), 1000);
        auto *visibility = tool->findChild<QQuickItem *>(QStringLiteral("toolVisibilityAction"));
        QVERIFY(visibility);
        QSet<QString> tabNames;
        bool reachedVisibility = false;
        for (int i = 0; i < 120; ++i) {
            QTest::keyClick(tool, Qt::Key_Tab);
            QQuickItem *focused = tool->activeFocusItem();
            if (!focused)
                continue;
            if (!focused->objectName().isEmpty())
                tabNames.insert(focused->objectName());
            if (focused == visibility && !reachedVisibility) {
                reachedVisibility = true;
                QVERIFY(activeVisible());
                QTest::keyClick(tool, Qt::Key_Space);
                QTRY_VERIFY_WITH_TIMEOUT(!activeVisible(), 1000);
            }
        }
        QVERIFY(reachedVisibility);
        const QSet<QString> requiredToolControls{
            QStringLiteral("toolVisibilityAction"),
            QStringLiteral("toolLockAction"),
            QStringLiteral("toolCloseAction"),
            QStringLiteral("toolOpacitySlider"),
            QStringLiteral("toolNormalModeButton"),
            QStringLiteral("toolMultiplyModeButton"),
            QStringLiteral("toolScreenModeButton"),
            QStringLiteral("toolDuplicateAction"),
            QStringLiteral("toolMoveUpAction"),
            QStringLiteral("toolMoveDownAction"),
            QStringLiteral("toolDeleteAction"),
            QStringLiteral("toolClearFrameAction"),
            QStringLiteral("toolClearAllAction"),
            QStringLiteral("toolConvertSharedAction"),
            QStringLiteral("toolMergeAction"),
            QStringLiteral("toolFlattenAction")};
        for (const QString &name : requiredToolControls)
            QVERIFY2(tabNames.contains(name), qPrintable(name));

        QTest::keyClick(tool, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!tool->isVisible(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(list->hasActiveFocus(), 1000);

        QVERIFY(QMetaObject::invokeMethod(dock, "activate",
                                          Q_ARG(QVariant, QStringLiteral("overlay"))));
        QTRY_VERIFY_WITH_TIMEOUT(tool->isVisible(), 1000);
        QVERIFY(QMetaObject::invokeMethod(tool, "convert",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("shared")))));
        QTRY_VERIFY_WITH_TIMEOUT(sheet->property("opened").toBool(), 1000);
        QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
        QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("opened").toBool(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(tool->activeFocusItem(), 1000);
        QVERIFY(tool->isVisible());
    }

    void layerToolTextAndFocusRingsStayInsideTheirCards()
    {
        QTest::failOnWarning();
        registerQmlTypes();
        DocumentModel document;
        QVERIFY(document.renameLayer(
            QStringLiteral("layer"),
            QStringLiteral("A deliberately long animated foreground layer name")));
        Theme theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        static InputLog quiet(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &quiet);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());

        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        auto *studio = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(studio);
        studio->resize(900, 700);
        studio->show();
        QVERIFY(QTest::qWaitForWindowExposed(studio));

        auto *dock = studio->findChild<QQuickItem *>(QStringLiteral("layerDock"));
        auto *tool = studio->findChild<QQuickWindow *>(QStringLiteral("layerToolWindow"));
        QVERIFY(dock);
        QVERIFY(tool);
        QVERIFY(QMetaObject::invokeMethod(dock, "activate",
                                          Q_ARG(QVariant, QStringLiteral("layer"))));
        QTRY_VERIFY_WITH_TIMEOUT(tool->isVisible(), 1000);

        auto *card = tool->findChild<QQuickItem *>(QStringLiteral("layerToolTargetCard"));
        auto *target = tool->findChild<QQuickItem *>(QStringLiteral("layerToolTargetText"));
        auto *structural = tool->findChild<QQuickItem *>(
            QStringLiteral("layerToolStructuralTargetText"));
        QVERIFY(card);
        QVERIFY(target);
        QVERIFY(structural);
        QCoreApplication::processEvents();
        const QPointF targetAt = target->mapToItem(card, QPointF());
        const QPointF structuralAt = structural->mapToItem(card, QPointF());
        QVERIFY(target->height() > 20.0);
        QVERIFY(targetAt.y() >= 0.0);
        QVERIFY(targetAt.y() + target->height() <= card->height() + 0.5);
        QVERIFY(structuralAt.y() + structural->height() <= card->height() + 0.5);

        const auto insideParent = [](QQuickItem *ring) {
            QQuickItem *owner = ring ? ring->parentItem() : nullptr;
            return owner && ring->x() >= 0.0 && ring->y() >= 0.0
                   && ring->x() + ring->width() <= owner->width() + 0.5
                   && ring->y() + ring->height() <= owner->height() + 0.5;
        };
        auto *chipRing = tool->findChild<QQuickItem *>(QStringLiteral("chipFocusRing"));
        auto *sectionRing = tool->findChild<QQuickItem *>(QStringLiteral("sectionFocusRing"));
        QVERIFY(chipRing);
        QVERIFY(sectionRing);
        QVERIFY(insideParent(chipRing));
        QVERIFY(insideParent(sectionRing));
    }

    void layerToolLifecycleKeepsOnePublishedSessionAndStableCliState()
    {
        registerQmlTypes();
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel document;
        QVERIFY(document.renameLayer(QStringLiteral("layer"), QStringLiteral("Base")));
        document.setEditScope(QStringLiteral("all-frames"));
        SessionPublisher publisher;
        publisher.follow(&document);
         const QByteArray before = publisher.snapshot();
         QVERIFY(!before.isEmpty());
         const QJsonObject beforeView = QJsonDocument::fromJson(before).object()
                                            .value(QStringLiteral("view")).toObject();
        QCOMPARE(beforeView.value(QStringLiteral("layerId")).toString(),
                 document.activeLayerId());
        QCOMPARE(beforeView.value(QStringLiteral("layerName")).toString(),
                 document.activeLayerName());
        QCOMPARE(beforeView.value(QStringLiteral("scope")).toString(), document.editScope());

        Theme theme;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        static InputLog silent(false);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &silent);
        engine.rootContext()->setContextProperty(QStringLiteral("shotSheet"), QString());
        QQmlComponent component(&engine,
            QUrl::fromLocalFile(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY(root);
        auto *studio = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(studio);
        auto *dock = studio->findChild<QQuickItem *>(QStringLiteral("layerDock"));
        auto *tool = studio->findChild<QQuickWindow *>(QStringLiteral("layerToolWindow"));
        QVERIFY(dock);
        QVERIFY(tool);
        QVERIFY(QMetaObject::invokeMethod(dock, "activate",
                                          Q_ARG(QVariant, QStringLiteral("layer"))));
        QTRY_VERIFY_WITH_TIMEOUT(tool->isVisible(), 1000);
        tool->setX(2100);
        tool->setY(90);
        tool->requestActivate();
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(tool, "closeTool"));
        QTRY_VERIFY_WITH_TIMEOUT(!tool->isVisible(), 1000);
         const QByteArray after = publisher.snapshot();
         QCOMPARE(after, before);
        QCOMPARE(document.activeLayerId(), QStringLiteral("layer"));
        const QJsonObject afterView = QJsonDocument::fromJson(after).object()
                                          .value(QStringLiteral("view")).toObject();
        QCOMPARE(afterView.value(QStringLiteral("layerId")).toString(), QStringLiteral("layer"));
        QCOMPARE(afterView.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Base"));
        QCOMPARE(afterView.value(QStringLiteral("scope")).toString(),
                 QStringLiteral("all-frames"));
        publisher.retire();
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

         const auto published = [&sessions] {
             return QJsonDocument::fromJson(sessions.snapshot()).object();
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
        QCOMPARE(session.value(QStringLiteral("version")).toInt(), 2);
        const QJsonObject initialView =
            session.value(QStringLiteral("view")).toObject();
        QCOMPARE(initialView.value(QStringLiteral("clip")).toString(),
                 QStringLiteral("idle"));
        QCOMPARE(initialView.value(QStringLiteral("frame")).toInt(), 0);
        QCOMPARE(initialView.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("layer"));
        QCOMPARE(initialView.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Layer"));
        QCOMPARE(initialView.value(QStringLiteral("scope")).toString(),
                 QStringLiteral("frame"));
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
        QCOMPARE(selection.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("layer"));
        QCOMPARE(selection.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Layer"));
        QCOMPARE(selection.value(QStringLiteral("x")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("y")).toInt(), 0);
        QCOMPARE(selection.value(QStringLiteral("width")).toInt(), 4);
        QCOMPARE(selection.value(QStringLiteral("height")).toInt(), 3);
        QCOMPARE(selection.value(QStringLiteral("count")).toInt(), 12);
         QCOMPARE(session.value(QStringLiteral("path")).toString(), drawing);
         QCOMPARE(session.value(QStringLiteral("view")).toObject()
                      .value(QStringLiteral("layerId")).toString(), QStringLiteral("layer"));

        QVERIFY(doc.save());
        session = published();
        QCOMPARE(session.value(QStringLiteral("dirty")).toBool(), false);
    }

    void sessionViewTracksLayerIdentityAndPlaybackSnapshot()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString drawing = runtime.path() + QStringLiteral("/layers.json");
        Document layered = Document::blank(4, 3);
        QVERIFY(layered.addLayer(QStringLiteral("hero"), QStringLiteral("Hero")));
        QVERIFY(layered.addFrame(QStringLiteral("idle"), 0, false));
        QVERIFY(writeDocument(drawing, layered));

        DocumentModel doc;
        QVERIFY(doc.open(drawing));
        SessionPublisher publisher;
        publisher.follow(&doc);

         const auto bytes = [&publisher] {
             return publisher.snapshot();
         };
        const auto json = [&bytes] {
            return QJsonDocument::fromJson(bytes()).object();
        };

        // A view exists without a selection, and changing playback's frame
        // publishes the latest atomic snapshot. Playback itself is local UI
        // state and is intentionally not part of the session contract.
        QJsonObject view = json().value(QStringLiteral("view")).toObject();
        QCOMPARE(view.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("layer"));
        QVERIFY(json().value(QStringLiteral("selection")).isNull());
        doc.setActiveLayerId(QStringLiteral("hero"));
        doc.setEditScope(QStringLiteral("all-frames"));
        doc.setFrame(1);
        view = json().value(QStringLiteral("view")).toObject();
        QCOMPARE(view.value(QStringLiteral("frame")).toInt(), 1);
        QCOMPARE(view.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("hero"));
        QCOMPARE(view.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Hero"));
        QCOMPARE(view.value(QStringLiteral("scope")).toString(),
                 QStringLiteral("all-frames"));
        QVERIFY(!json().contains(QStringLiteral("playing")));

        doc.setSelection(0, 0, 1, 1);
        const QJsonObject selection =
            json().value(QStringLiteral("selection")).toObject();
        QCOMPARE(selection.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("hero"));
        QCOMPARE(selection.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Hero"));
        QCOMPARE(selection.value(QStringLiteral("count")).toInt(), 4);

        // Rename and reorder preserve the immutable active ID; the name is
        // refreshed from the reloaded document. Deleting it falls back to the
        // first remaining layer in document order.
        Document renamed = layered;
        QVERIFY(renamed.renameLayer(QStringLiteral("hero"),
                                    QStringLiteral("Renamed Hero")));
        QVERIFY(renamed.moveLayer(QStringLiteral("hero"), 0));
        QVERIFY(writeDocument(drawing, renamed));
        QVERIFY(doc.reloadFromDisk());
        view = json().value(QStringLiteral("view")).toObject();
        QCOMPARE(view.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("hero"));
        QCOMPARE(view.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Renamed Hero"));
        QVERIFY(json().value(QStringLiteral("selection")).isNull());

        QVERIFY(renamed.removeLayer(QStringLiteral("hero")));
        QVERIFY(writeDocument(drawing, renamed));
        QVERIFY(doc.reloadFromDisk());
        view = json().value(QStringLiteral("view")).toObject();
        QCOMPARE(view.value(QStringLiteral("layerId")).toString(),
                 QStringLiteral("layer"));
        QCOMPARE(view.value(QStringLiteral("layerName")).toString(),
                 QStringLiteral("Layer"));

        // QSaveFile plus fixed insertion order makes the same no-selection
        // state byte-stable after a selection round trip.
        const QByteArray stable = bytes();
        doc.setSelection(0, 0, 0, 0);
        doc.clearSelection();
        QCOMPARE(bytes(), stable);
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
         QByteArray snapshot = sessions.snapshot();
         QVERIFY(!snapshot.isEmpty());

         sessions.retire();
         QVERIFY(!sessions::readPublisher(QCoreApplication::applicationPid(), &snapshot));
    }

    void twoStudiosPublishBesideEachOther()
    {
         // One abstract endpoint per process: two publishers never merge and
         // never overwrite each other.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        DocumentModel doc;
        SessionPublisher mine;
        mine.follow(&doc);

         doc.paint(2, 2, QStringLiteral("R"));   // rewrites ours, only ours
         QJsonObject ours = QJsonDocument::fromJson(mine.snapshot()).object();
         QVERIFY(!ours.isEmpty());
         QVERIFY(ours.value(QStringLiteral("dirty")).toBool());
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
         QJsonObject session = QJsonDocument::fromJson(publisher.snapshot()).object();
         QVERIFY(!session.isEmpty());
         QCOMPARE(session.value(QStringLiteral("path")).toString(), QString());

        // Put the suite's config back the way initTestCase found it.
        qputenv("OMAPIXEL_CONFIG_PATH", "/nonexistent/omapixel-tests.toml");
        Config::shared().load();
    }

    // ---------------------------------------------------------------- where

    void whereFindsTheStudioHoldingADocument()
    {
        // The read side of the session contract must inspect a real Studio
        // process. A test runner is deliberately not accepted as a Studio.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString drawing =
            QDir(runtime.path()).absoluteFilePath(QStringLiteral("heart.json"));
        QVERIFY(writeDocument(drawing, sample()));

        QProcess studio;
        ScopedProcessCleanup cleanup{{&studio}};
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
        studio.setProcessEnvironment(environment);
        studio.start(QStringLiteral(SOURCE_DIR "/build/bin/omapixel-studio"), {drawing});
        QVERIFY(studio.waitForStarted(5000));
        QByteArray snapshot;
        QString publisherError;
         QTRY_VERIFY2_WITH_TIMEOUT(
             sessions::readPublisher(studio.processId(), &snapshot, &publisherError),
             qPrintable(publisherError), 5000);
        const QString canonicalDrawing = QFileInfo(drawing).canonicalFilePath();
        QList<sessions::Entry> found;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            found = sessions::live();
            return found.size() == 1 && found.first().pid == studio.processId()
                && found.first().path == canonicalDrawing;
        }()), 30000);
        QCOMPARE(found.first().pid, studio.processId());
        QCOMPARE(found.first().path, canonicalDrawing);
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
        // Two processes on one document: two entries, because inventing a
        // winner would be worse than making the caller choose.
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString drawing = runtime.path() + QStringLiteral("/shared.json");
        QVERIFY(writeDocument(drawing, sample()));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
        QProcess first;
        QProcess second;
        ScopedProcessCleanup cleanup{{&first, &second}};
        first.setProcessEnvironment(environment);
        second.setProcessEnvironment(environment);
        const QString studioPath = QStringLiteral(SOURCE_DIR "/build/bin/omapixel-studio");
        first.start(studioPath, {drawing});
        second.start(studioPath, {drawing});
        QVERIFY(first.waitForStarted(5000));
        QVERIFY(second.waitForStarted(5000));
        QByteArray firstSnapshot;
        QByteArray secondSnapshot;
        QString firstError;
        QTRY_VERIFY2_WITH_TIMEOUT(
            sessions::readPublisher(first.processId(), &firstSnapshot, &firstError),
            qPrintable(firstError), 15000);
        QString secondError;
         QTRY_VERIFY2_WITH_TIMEOUT(
             sessions::readPublisher(second.processId(), &secondSnapshot, &secondError),
             qPrintable(secondError), 15000);
        const QString canonicalDrawing = QFileInfo(drawing).canonicalFilePath();
        QCOMPARE(QJsonDocument::fromJson(firstSnapshot).object()
                     .value(QStringLiteral("path")).toString(), canonicalDrawing);
        QCOMPARE(QJsonDocument::fromJson(secondSnapshot).object()
                     .value(QStringLiteral("path")).toString(), canonicalDrawing);
         const QSet<qint64> expectedPids{first.processId(), second.processId()};
         QList<sessions::Entry> found;
          QTRY_VERIFY_WITH_TIMEOUT(([&] {
              found = sessions::live();
             if (found.size() != expectedPids.size())
                 return false;
             QSet<qint64> pids;
             for (const sessions::Entry &entry : found) {
                 if (entry.path != canonicalDrawing)
                     return false;
                 pids.insert(entry.pid);
             }
             return pids == expectedPids;
          }()), 5000);
          QCOMPARE(found.size(), expectedPids.size());
     }

    void whereDoesNotWaitForAnUnrelatedStoppedStudio()
    {
        QTemporaryDir runtime;
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

        const QString wantedDrawing = runtime.path() + QStringLiteral("/wanted.json");
        const QString unrelatedDrawing = runtime.path() + QStringLiteral("/unrelated.json");
        QVERIFY(writeDocument(wantedDrawing, sample()));
        QVERIFY(writeDocument(unrelatedDrawing, sample()));

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
        QProcess unrelated;
        QProcess wanted;
        ScopedProcessCleanup cleanup{{&unrelated, &wanted}};
        unrelated.setProcessEnvironment(environment);
        wanted.setProcessEnvironment(environment);
        const QString studioPath = QStringLiteral(SOURCE_DIR "/build/bin/omapixel-studio");
        unrelated.start(studioPath, {unrelatedDrawing});
        wanted.start(studioPath, {wantedDrawing});
        QVERIFY(unrelated.waitForStarted(5000));
        QVERIFY(wanted.waitForStarted(5000));

        QByteArray snapshot;
        QTRY_VERIFY_WITH_TIMEOUT(sessions::readPublisher(unrelated.processId(), &snapshot),
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sessions::readPublisher(wanted.processId(), &snapshot),
                                 5000);
        QVERIFY(::kill(pid_t(unrelated.processId()), SIGSTOP) == 0);

        const QString canonicalWanted = QFileInfo(wantedDrawing).canonicalFilePath();
        QElapsedTimer timer;
        timer.start();
        const QList<sessions::Entry> found = sessions::live(canonicalWanted);
        const qint64 elapsed = timer.elapsed();
        QVERIFY(::kill(pid_t(unrelated.processId()), SIGCONT) == 0);

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().pid, wanted.processId());
        QCOMPARE(found.first().path, canonicalWanted);
        QVERIFY2(elapsed < 750,
                 qPrintable(QStringLiteral("discovery took %1 ms").arg(elapsed)));
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
        registerQmlTypes();

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
        registerQmlTypes();

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
        registerQmlTypes();
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
        const int additions = Document::maxPaletteSlots - doc.document().palette().size();
        for (int i = 0; i < additions; ++i) {
            const QString slot = doc.freeSlot();
            QVERIFY2(!slot.isEmpty(), "ran out of slots before the 256-slot limit");
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
        QVERIFY(doc.freeSlot().isEmpty());

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
        registerQmlTypes();

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
        registerQmlTypes();

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
        registerQmlTypes();

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
        StudioHarness studio;
        studio.document.reset(16, 16);
        QVERIFY2(studio.open(QSize(1200, 860)), qPrintable(studio.error));

        auto *canvas = qobject_cast<QQuickItem *>(studio.named(QStringLiteral("canvas keys")));
        QVERIFY(canvas);
        const auto walk = [&](bool reverse, bool *complete, QStringList *invalid) {
            QSet<QQuickItem *> visited;
            canvas->forceActiveFocus();
            QTest::keyClick(studio.window, reverse ? Qt::Key_Backtab : Qt::Key_Tab);
            QQuickItem *first = studio.window->activeFocusItem();
            if (!first)
                return visited;
            visited.insert(first);
            for (int step = 0; step < 200; ++step) {
                QTest::keyClick(studio.window,
                                reverse ? Qt::Key_Backtab : Qt::Key_Tab);
                QQuickItem *here = studio.window->activeFocusItem();
                if (!here)
                    continue;
                if (!here->isVisible() || !here->isEnabled())
                    invalid->append(here->objectName().isEmpty()
                                        ? QString::fromUtf8(here->metaObject()->className())
                                        : here->objectName());
                if (here == first) {
                    *complete = true;
                    break;
                }
                visited.insert(here);
            }
            return visited;
        };

        bool forwardComplete = false;
        QStringList invalidForward;
        const QSet<QQuickItem *> forward = walk(false, &forwardComplete, &invalidForward);
        bool reverseComplete = false;
        QStringList invalidReverse;
        const QSet<QQuickItem *> reverse = walk(true, &reverseComplete, &invalidReverse);
        QVERIFY2(forwardComplete, "Tab did not complete a focus cycle");
        QVERIFY2(reverseComplete, "Shift+Tab did not complete a focus cycle");
        QVERIFY2(invalidForward.isEmpty(), qPrintable(invalidForward.join(QStringLiteral(", "))));
        QVERIFY2(invalidReverse.isEmpty(), qPrintable(invalidReverse.join(QStringLiteral(", "))));
        const auto labels = [](const QSet<QQuickItem *> &items) {
            QStringList result;
            for (QQuickItem *item : items)
                result << (item->objectName().isEmpty()
                               ? QString::fromUtf8(item->metaObject()->className())
                               : item->objectName());
            result.sort();
            return result;
        };
        const auto withoutFocusProxies = [](QSet<QQuickItem *> items) {
            for (auto it = items.begin(); it != items.end();) {
                if ((*it)->objectName().startsWith(QLatin1String("layerRow_")))
                    it = items.erase(it);
                else
                    ++it;
            }
            return items;
        };
        const QSet<QQuickItem *> logicalForward = withoutFocusProxies(forward);
        const QSet<QQuickItem *> logicalReverse = withoutFocusProxies(reverse);
        const QStringList onlyForward = labels(logicalForward - logicalReverse);
        const QStringList onlyReverse = labels(logicalReverse - logicalForward);
        QVERIFY2(onlyForward.isEmpty() && onlyReverse.isEmpty(),
                 qPrintable(QStringLiteral("focus order is asymmetric; forward-only: %1; reverse-only: %2")
                                .arg(onlyForward.join(QStringLiteral(", ")),
                                     onlyReverse.join(QStringLiteral(", ")))));

        const QStringList required{
            QStringLiteral("toolsFirstControl"), QStringLiteral("dockSplitter"),
            QStringLiteral("layerList"), QStringLiteral("timelineAddFrameControl"),
            QStringLiteral("paletteSectionHeader"), QStringLiteral("previewSectionHeader"),
            QStringLiteral("spriteSectionHeader"), QStringLiteral("referenceSectionHeader"),
            QStringLiteral("historySectionHeader")};
        const auto reaches = [](const QSet<QQuickItem *> &items, QQuickItem *target) {
            for (QQuickItem *item : items) {
                for (QQuickItem *at = item; at; at = at->parentItem()) {
                    if (at == target)
                        return true;
                }
            }
            return false;
        };
        for (const QString &name : required) {
            auto *item = qobject_cast<QQuickItem *>(studio.named(name));
            QVERIFY2(item, qPrintable(name));
            QVERIFY2(reaches(forward, item), qPrintable(name + QStringLiteral(" is outside Tab order")));
        }

        studio.window->resize(900, 560);
        QCoreApplication::processEvents();
        bool compactComplete = false;
        QStringList invalidCompact;
        const QSet<QQuickItem *> compact = walk(false, &compactComplete, &invalidCompact);
        QVERIFY2(compactComplete, "Tab did not complete at the minimum window size");
        QVERIFY2(invalidCompact.isEmpty(), qPrintable(invalidCompact.join(QStringLiteral(", "))));
        for (const QString &name : required)
            QVERIFY2(reaches(compact, qobject_cast<QQuickItem *>(studio.named(name))), qPrintable(name));

        // The menu bar answers to the keyboard too. Nothing in the window
        // should need a pointer, and a menu you can only open by clicking is
        // the whole command set behind one.
        QTest::keyClick(studio.window, Qt::Key_F10);
        QTest::qWait(30);
        QQuickItem *onMenus = studio.window->activeFocusItem();
        QVERIFY2(onMenus && (onMenus->inherits("QQuickMenuBar")
                             || onMenus->inherits("QQuickMenuBarItem")
                             || (onMenus->parentItem()
                                  && onMenus->parentItem()->inherits("QQuickMenuBar"))),
                 "F10 did not put the keyboard on the menu bar");

        QObject *fileMenu = studio.named(QStringLiteral("fileMenu"));
        QObject *newSheet = studio.named(QStringLiteral("newSheet"));
        QVERIFY(fileMenu);
        QVERIFY(newSheet);
        QTest::keyClick(studio.window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(fileMenu->property("opened").toBool(), 1000);
        QTest::keyClick(studio.window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(newSheet->property("opened").toBool(), 1000);
        QTest::keyClick(studio.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!newSheet->property("opened").toBool(), 1000);

        // Escape brings the keyboard back to the drawing from wherever Tab
        // left it, so getting lost is one key rather than a hunt.
        QTest::keyClick(studio.window, Qt::Key_Right);
        QVERIFY2(studio.root->property("caretColumn").toInt() >= 0,
                 "escape did not hand the keyboard back to the drawing");
    }

    void commandPaletteSearchesRunsAndNavigatesEveryRegion()
    {
        QTest::failOnWarning();
        registerQmlTypes();
        QQmlEngine engine;
        DocumentModel document;
        document.addFrame(false);
        Theme theme;
        static InputLog quiet(false);
        engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &Config::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("T"), &Strings::shared());
        engine.rootContext()->setContextProperty(QStringLiteral("log"), &quiet);
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

        auto *palette = window->findChild<QObject *>(QStringLiteral("commandPalette"));
        auto *registry = window->findChild<QObject *>(QStringLiteral("commandRegistry"));
        auto *search = window->findChild<QQuickItem *>(QStringLiteral("commandPaletteSearch"));
        auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("navigationOverlay"));
        auto *layerList = window->findChild<QQuickItem *>(QStringLiteral("layerList"));
        auto *layerTool = window->findChild<QQuickWindow *>(QStringLiteral("layerToolWindow"));
        auto *timelineFirst = window->findChild<QQuickItem *>(
            QStringLiteral("timelineFirstControl"));
        auto *canvasKeys = window->findChild<QQuickItem *>(QStringLiteral("canvas keys"));
        QVERIFY(palette);
        QVERIFY(registry);
        QVERIFY(search);
        QVERIFY(overlay);
        QVERIFY(layerList);
        QVERIFY(layerTool);
        QVERIFY(timelineFirst);
        QVERIFY(canvasKeys);

        QVERIFY(QMetaObject::invokeMethod(root.data(), "openCommandPalette"));
        QTRY_VERIFY_WITH_TIMEOUT(root->property("commandPaletteOpen").toBool(), 1000);
        const QVariantList commands = root->property("commandEntries").toList();
        QVERIFY(commands.size() > 70);
        QSet<QString> ids;
        for (const QVariant &value : commands)
            ids.insert(value.toMap().value(QStringLiteral("id")).toString());
        const QSet<QString> required{
            QStringLiteral("navigate"), QStringLiteral("file.save"),
            QStringLiteral("canvas.tool.pencil"), QStringLiteral("layers.addAnimated"),
            QStringLiteral("layers.flatten"), QStringLiteral("inspector.reference.choose"),
            QStringLiteral("inspector.canvasSize"), QStringLiteral("timeline.addFrame"),
            QStringLiteral("timeline.selectFrame")};
        for (const QString &id : required)
            QVERIFY2(ids.contains(id), qPrintable(id));
        QDirIterator providers(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                               {QStringLiteral("*Commands.qml")}, QDir::Files);
        const QRegularExpression commandDefinition(
            QStringLiteral("commandId\\s*:\\s*\"([^\"]+)\""));
        while (providers.hasNext()) {
            QFile provider(providers.next());
            QVERIFY(provider.open(QIODevice::ReadOnly));
            auto definitions = commandDefinition.globalMatch(
                QString::fromUtf8(provider.readAll()));
            while (definitions.hasNext()) {
                const QString id = definitions.next().captured(1);
                QVERIFY2(ids.contains(id),
                         qPrintable(id + QStringLiteral(" is absent from the keyboard palette")));
            }
        }
        int selectableLayers = 0;
        int selectableClips = 0;
        int selectableFrames = 0;
        int selectableSlots = 0;
        for (const QVariant &value : commands) {
            const QString id = value.toMap().value(QStringLiteral("id")).toString();
            selectableLayers += id == QLatin1String("layers.select");
            selectableClips += id == QLatin1String("timeline.selectClip");
            selectableFrames += id == QLatin1String("timeline.selectFrame");
            selectableSlots += id == QLatin1String("palette.select");
        }
        QCOMPARE(selectableLayers, document.layers().size());
        QCOMPARE(selectableClips, document.clips().size());
        QCOMPARE(selectableFrames, document.frameCount());
        QCOMPARE(selectableSlots, document.palette().size());
        for (const QVariant &value : commands) {
            const QVariantMap descriptor = value.toMap();
            const QStringList fields{QStringLiteral("id"), QStringLiteral("args"),
                                     QStringLiteral("label"), QStringLiteral("group"),
                                     QStringLiteral("keywords"), QStringLiteral("shortcut"),
                                     QStringLiteral("enabled"), QStringLiteral("checkable"),
                                     QStringLiteral("checked")};
            for (const QString &field : fields)
                QVERIFY2(descriptor.contains(field), qPrintable(field));
        }
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("commandPaletteOpen").toBool(), 1000);

        const auto type = [window](const QString &text) {
            for (const QChar character : text)
                QTest::keyClick(window, character.toLatin1());
        };
        const auto openNavigate = [&] {
            QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
            QTRY_VERIFY_WITH_TIMEOUT(root->property("commandPaletteOpen").toBool(), 1000);
            QVERIFY(window->activeFocusItem());
            QVERIFY(window->activeFocusItem()->inherits("QQuickTextInput"));
            type(QStringLiteral("navigate"));
            QTRY_VERIFY_WITH_TIMEOUT(palette->property("resultCount").toInt() >= 4, 1000);
            QTest::keyClick(window, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(root->property("navigationMode").toBool(), 1000);
        };

        openNavigate();
        QTest::keyClick(window, Qt::Key_2);
        QTRY_VERIFY_WITH_TIMEOUT(layerList->hasActiveFocus(), 1000);

        openNavigate();
        QTest::keyClick(window, Qt::Key_3);
        QTRY_VERIFY_WITH_TIMEOUT(timelineFirst->hasActiveFocus(), 1000);

        openNavigate();
        QTest::keyClick(window, Qt::Key_1);
        QTRY_VERIFY_WITH_TIMEOUT(canvasKeys->hasActiveFocus(), 1000);

        const int layersBefore = document.layers().size();
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(root->property("commandPaletteOpen").toBool(), 1000);
        type(QStringLiteral("add animated layer"));
        QTRY_COMPARE(palette->property("resultCount").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(document.layers().size(), layersBefore + 1);

        const auto invokeCommand = [registry](const QString &id,
                                               const QVariantMap &args = QVariantMap{}) {
            return QMetaObject::invokeMethod(registry, "invoke",
                                              Q_ARG(QVariant, id),
                                              Q_ARG(QVariant, args));
        };

        QVariant executed;
        QVERIFY(QMetaObject::invokeMethod(registry, "invoke",
                                          Q_RETURN_ARG(QVariant, executed),
                                          Q_ARG(QVariant, QStringLiteral("edit.copy")),
                                          Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(!executed.toBool());
        QVERIFY(QMetaObject::invokeMethod(registry, "invoke",
                                          Q_RETURN_ARG(QVariant, executed),
                                          Q_ARG(QVariant, QStringLiteral("unknown.command")),
                                          Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(!executed.toBool());

        root->setProperty("tool", QStringLiteral("eraser"));
        const QString paletteSlot = document.palette().value(1).toMap()
                                        .value(QStringLiteral("slot")).toString();
        QVERIFY(invokeCommand(QStringLiteral("palette.select"),
                              {{QStringLiteral("slot"), paletteSlot}}));
        QCOMPARE(root->property("tool").toString(), QStringLiteral("pencil"));

        document.setFrame(0);
        QVERIFY(invokeCommand(QStringLiteral("timeline.selectFrame"),
                              {{QStringLiteral("frame"), QStringLiteral("1junk")}}));
        QCOMPARE(document.frame(), 0);
        root->setProperty("referenceAlpha", 0.5);
        QVERIFY(invokeCommand(QStringLiteral("inspector.reference.setOpacity"),
                              {{QStringLiteral("percent"), 125}}));
        QCOMPARE(root->property("referenceAlpha").toDouble(), 0.5);

        const QString originalLayerId = document.layers().first().toMap()
                                            .value(QStringLiteral("id")).toString();
        QVERIFY(document.addLayer(QStringLiteral("command-target"),
                                  QStringLiteral("command target")));
        QVERIFY(document.moveLayer(originalLayerId, document.layers().size() - 1));
        QVERIFY(invokeCommand(QStringLiteral("layers.select"),
                              {{QStringLiteral("layerId"), originalLayerId}}));
        QCOMPARE(document.activeLayerId(), originalLayerId);

        const QVariantMap originalClip = document.clips().first().toMap();
        const QString originalClipId = originalClip.value(QStringLiteral("id")).toString();
        const QString originalClipName = originalClip.value(QStringLiteral("name")).toString();
        document.addClip(QStringLiteral("command clip"));
        document.renameClip(originalClipName, QStringLiteral("renamed command clip"));
        document.setClip(QStringLiteral("command clip"));
        QVERIFY(invokeCommand(QStringLiteral("timeline.selectClip"),
                              {{QStringLiteral("clipId"), originalClipId}}));
        QCOMPARE(document.activeClipId(), originalClipId);

        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(root->property("commandPaletteOpen").toBool(), 1000);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!root->property("commandPaletteOpen").toBool(), 1000);

        QVERIFY(document.addLayer(QStringLiteral("keyboard-layer"),
                                  QStringLiteral("Keyboard layer")));
        openNavigate();
        QTest::keyClick(window, Qt::Key_2);
        QTRY_VERIFY_WITH_TIMEOUT(layerList->hasActiveFocus(), 1000);
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(layerTool->isVisible(), 1000);
        QCOMPARE(document.activeLayerId(), QStringLiteral("keyboard-layer"));
    }

    void keyboardFocusContractsCoverSheetsFieldsAndDisabledRegions()
    {
        QTest::failOnWarning();
        StudioHarness studio;
        studio.document.reset(16, 16);
        QVERIFY2(studio.open(), qPrintable(studio.error));

        auto *canvas = qobject_cast<QQuickItem *>(studio.named(QStringLiteral("canvas keys")));
        auto *timelineAdd = qobject_cast<QQuickItem *>(
            studio.named(QStringLiteral("timelineAddFrameControl")));
        auto *paletteSection = studio.named(QStringLiteral("paletteSection"));
        auto *eraser = qobject_cast<QQuickItem *>(studio.named(QStringLiteral("toolEraser")));
        QVERIFY(canvas);
        QVERIFY(timelineAdd);
        QVERIFY(paletteSection);
        QVERIFY(eraser);

        // Region navigation must never land on Play when one frame makes it
        // unusable. The first usable timeline operation is Add frame.
        QVERIFY(studio.invoke(QStringLiteral("navigate.timeline")));
        QTRY_VERIFY_WITH_TIMEOUT(timelineAdd->hasActiveFocus(), 1000);
        const int framesBefore = studio.document.frameCount();
        QTest::keyClick(studio.window, Qt::Key_Enter);
        QTRY_COMPARE(studio.document.frameCount(), framesBefore + 1);

        auto *timelinePlay = qobject_cast<QQuickItem *>(
            studio.named(QStringLiteral("timelineFirstControl")));
        QVERIFY(timelinePlay);
        QVERIFY(studio.invoke(QStringLiteral("navigate.timeline")));
        QTRY_VERIFY_WITH_TIMEOUT(timelinePlay->hasActiveFocus(), 1000);
        studio.document.removeFrame();
        QCOMPARE(studio.document.frameCount(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(timelineAdd->hasActiveFocus(), 1000);
        studio.root->setProperty("caretColumn", 0);
        studio.root->setProperty("caretRow", 0);
        QCOMPARE(studio.document.slotAt(0, 0), QStringLiteral("."));
        QTest::keyClick(studio.window, Qt::Key_Enter);
        QCOMPARE(studio.document.slotAt(0, 0), QStringLiteral("."));
        QCOMPARE(studio.document.frameCount(), 2);

        // Keypad Enter is an activation key too. This catches custom controls
        // that implement Return but silently ignore the second Enter keycode.
        eraser->forceActiveFocus();
        QTest::keyClick(studio.window, Qt::Key_Enter);
        QCOMPARE(studio.root->property("tool").toString(), QStringLiteral("eraser"));
        QVERIFY(QMetaObject::invokeMethod(paletteSection, "focusHeader"));
        const bool sectionWasOpen = paletteSection->property("open").toBool();
        QTest::keyClick(studio.window, Qt::Key_Enter);
        QTRY_COMPARE(paletteSection->property("open").toBool(), !sectionWasOpen);

        // Go-to owns the arrows while its fields are active, and closing it
        // returns to the canvas rather than the unrelated Layer tool origin.
        studio.root->setProperty("caretColumn", 4);
        studio.root->setProperty("caretRow", 5);
        QVERIFY(studio.invoke(QStringLiteral("view.goTo")));
        auto *goTo = studio.named(QStringLiteral("goToSheet"));
        auto *goToX = studio.named(QStringLiteral("goToX"));
        QVERIFY(goTo);
        QVERIFY(goToX);
        QTRY_VERIFY_WITH_TIMEOUT(goTo->property("opened").toBool(), 1000);
        QObject *goToInput = goToX->property("input").value<QObject *>();
        QTRY_COMPARE_WITH_TIMEOUT(studio.window->activeFocusItem(), goToInput, 1000);
        QTest::keyClick(studio.window, Qt::Key_Up);
        QCOMPARE(studio.root->property("caretColumn").toInt(), 4);
        QCOMPARE(studio.root->property("caretRow").toInt(), 5);
        QTest::keyClick(studio.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!goTo->property("opened").toBool(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(canvas->hasActiveFocus(), 1000);

        // Every modal sheet declares a deterministic first focus target and
        // Escape returns to the documented canvas fallback.
        const QList<QPair<QString, QString>> sheets{
            {QStringLiteral("newSheet"), QStringLiteral("newColumns")},
            {QStringLiteral("exportSheet"), QStringLiteral("exportChecker")},
            {QStringLiteral("unsavedSheet"), QStringLiteral("unsavedSave")},
            {QStringLiteral("trimSheet"), QStringLiteral("trimConfirm")},
            {QStringLiteral("layerSheet"), QStringLiteral("layerConfirm")}};
        for (const auto &[sheetName, focusName] : sheets) {
            QObject *sheet = studio.named(sheetName);
            QObject *focusObject = studio.named(focusName);
            QVERIFY2(sheet, qPrintable(sheetName));
            QVERIFY2(focusObject, qPrintable(focusName));
            QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
            QTRY_VERIFY_WITH_TIMEOUT(sheet->property("opened").toBool(), 1000);
            QObject *expected = focusObject;
            if (focusObject->property("input").isValid())
                expected = focusObject->property("input").value<QObject *>();
            QTRY_COMPARE_WITH_TIMEOUT(studio.window->activeFocusItem(), expected, 1000);
            QTest::keyClick(studio.window, Qt::Key_Escape);
            QTRY_VERIFY_WITH_TIMEOUT(!sheet->property("opened").toBool(), 1000);
            if (sheetName == QLatin1String("layerSheet")) {
                auto *layerList = qobject_cast<QQuickItem *>(
                    studio.named(QStringLiteral("layerList")));
                QVERIFY(layerList);
                QTRY_VERIFY_WITH_TIMEOUT(layerList->hasActiveFocus(), 1000);
            } else {
                QTRY_VERIFY_WITH_TIMEOUT(canvas->hasActiveFocus(), 1000);
            }
        }

        // Search-list arrows belong to the command palette, not to the canvas.
        studio.root->setProperty("caretColumn", 7);
        studio.root->setProperty("caretRow", 8);
        canvas->forceActiveFocus();
        QVERIFY(studio.invoke(QStringLiteral("command.palette")));
        auto *palette = studio.named(QStringLiteral("commandPalette"));
        QVERIFY(palette);
        QTRY_VERIFY_WITH_TIMEOUT(palette->property("opened").toBool(), 1000);
        QTest::keyClick(studio.window, Qt::Key_Down);
        QCOMPARE(studio.root->property("caretColumn").toInt(), 7);
        QCOMPARE(studio.root->property("caretRow").toInt(), 8);
        QTest::keyClick(studio.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(canvas->hasActiveFocus(), 1000);
    }

    void theColourPanelIsDrivenFromTheKeyboardAlone()
    {
        // Opening a search panel and then having to reach for the mouse to
        // type in it defeats the point of having it on a key.
        registerQmlTypes();

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
        registerQmlTypes();

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
        QJsonObject canvas;
        canvas.insert(QStringLiteral("width"), Document::maxDimension + 1);
        canvas.insert(QStringLiteral("height"), 2);
        QJsonObject clip;
        clip.insert(QStringLiteral("id"), QStringLiteral("idle"));
        clip.insert(QStringLiteral("name"), QStringLiteral("idle"));
        clip.insert(QStringLiteral("fps"), 8);
        clip.insert(QStringLiteral("frameCount"), 1);
        QJsonArray clips{clip};
        QJsonObject layer;
        layer.insert(QStringLiteral("id"), QStringLiteral("layer"));
        layer.insert(QStringLiteral("name"), QStringLiteral("Layer"));
        layer.insert(QStringLiteral("visible"), true);
        layer.insert(QStringLiteral("locked"), false);
        layer.insert(QStringLiteral("opacity"), 255);
        layer.insert(QStringLiteral("mode"), QStringLiteral("normal"));
        layer.insert(QStringLiteral("storage"), QStringLiteral("shared"));
        layer.insert(QStringLiteral("cels"), QJsonArray{QJsonObject{
            {QStringLiteral("scope"), QStringLiteral("all")},
            {QStringLiteral("rows"), QJsonArray{QStringLiteral(".."), QStringLiteral("..")}}}});
        QJsonObject root;
        root.insert(QStringLiteral("version"), 2);
        root.insert(QStringLiteral("canvas"), canvas);
        root.insert(QStringLiteral("palette"), QJsonArray());
        root.insert(QStringLiteral("clips"), clips);
        root.insert(QStringLiteral("layers"), QJsonArray{layer});

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
        registerQmlTypes();

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

    void semanticCommandsHaveOneCanonicalDefinition()
    {
        QSet<QString> commandIds;
        QStringList duplicates;
        const QRegularExpression definition(
            QStringLiteral("commandId\\s*:\\s*\"([^\"]+)\""));
        QDirIterator qml(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                         {QStringLiteral("*Commands.qml")}, QDir::Files);
        while (qml.hasNext()) {
            QFile file(qml.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString body = QString::fromUtf8(file.readAll());
            auto found = definition.globalMatch(body);
            while (found.hasNext()) {
                const QString id = found.next().captured(1);
                if (commandIds.contains(id))
                    duplicates << id;
                commandIds.insert(id);
            }
        }
        QVERIFY2(commandIds.size() > 70, "the semantic command surface is incomplete");
        QVERIFY2(duplicates.isEmpty(), qPrintable(duplicates.join(QStringLiteral(", "))));

        QFile main(QStringLiteral(SOURCE_DIR "/src/gui/qml/Main.qml"));
        QVERIFY(main.open(QIODevice::ReadOnly));
        const QString body = QString::fromUtf8(main.readAll());
        QVERIFY(!body.contains(QStringLiteral("runCommand")));
        QVERIFY(!body.contains(QStringLiteral("buildCommandEntries")));
        QVERIFY(!body.contains(QStringLiteral("commandIndex")));
        QVERIFY(!body.contains(QRegularExpression(
            QStringLiteral("(?:layers\\.select|timeline\\.(?:clip|frame)|palette\\.select)\\.\""))));
    }

    void pointerOperationsDeclareAKeyboardRoute()
    {
        const QSet<QString> keyboardControls{
            QStringLiteral("Chip.qml"), QStringLiteral("ToolButton.qml"),
            QStringLiteral("LayerAction.qml"), QStringLiteral("Section.qml")};
        QStringList missing;
        QDirIterator qml(QStringLiteral(SOURCE_DIR "/src/gui/qml"),
                         {QStringLiteral("*.qml")}, QDir::Files);
        while (qml.hasNext()) {
            const QString path = qml.next();
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString body = QString::fromUtf8(file.readAll());
            const QString name = QFileInfo(path).fileName();
            if (keyboardControls.contains(name)) {
                QVERIFY2(body.contains(QStringLiteral("activeFocusOnTab")),
                         qPrintable(name + QStringLiteral(" is clickable but not tabbable")));
                QVERIFY2(body.contains(QStringLiteral("Keys.onSpacePressed")), qPrintable(name));
                QVERIFY2(body.contains(QStringLiteral("Keys.onReturnPressed")), qPrintable(name));
                QVERIFY2(body.contains(QStringLiteral("Keys.onEnterPressed")), qPrintable(name));
            }

            const QStringList lines = body.split(QLatin1Char('\n'));
            for (int line = 0; line < lines.size(); ++line) {
                if (!lines.at(line).contains(QRegularExpression(
                        QStringLiteral("\\b(?:TapHandler|MouseArea)\\s*\\{"))))
                    continue;
                if (keyboardControls.contains(name))
                    continue;
                const int first = std::max(0, line - 2);
                const int last = std::min(int(lines.size()) - 1, line + 15);
                QString context;
                for (int at = first; at <= last; ++at)
                    context += lines.at(at) + QLatin1Char('\n');
                const bool commandRoute = context.contains(QRegularExpression(
                    QStringLiteral("(?:commands\\.invoke|commandRequested)\\s*\\(")));
                const bool localRoute = context.contains(
                    QStringLiteral("keyboard-equivalent:"));
                if (!commandRoute && !localRoute)
                    missing << QStringLiteral("%1:%2").arg(name).arg(line + 1);
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("pointer operation has no keyboard route: ")
                            + missing.join(QStringLiteral(", "))));
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
