// omapixel — the studio.
//
// A second front end over the same core the CLI drives. It owns the window and
// the input, and no rules: `DocumentModel` adapts types and calls `Document`,
// and `PixelGridItem` blits what `render::` produced. Anything either of them
// starts deciding on its own is a behaviour the CLI cannot reach and the tests
// do not cover.

#include "Config.h"
#include "DocumentModel.h"
#include "InputLog.h"
#include "Output.h"
#include "SessionPublisher.h"
#include "Strings.h"
#include "PixelGridItem.h"
#include "Theme.h"
#include "TextSafety.h"

#include <QGuiApplication>
#include <QBuffer>
#include <QIcon>
#include <QFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QtGlobal>

#include "version.h"

#include <cstdio>

namespace {

QString safeDiagnostic(const QString &text)
{
    return omapixel::text::escapeForTerminal(text);
}

struct InternalImageImport
{
    bool requested = false;
    QString path;
    int scale = 0;
    int width = 0;
    int height = 0;
    QString fit = QStringLiteral("contain");
};

bool parseInternalImageImport(const QStringList &arguments,
                              InternalImageImport *import, QString *error)
{
    bool sawInternal = false;
    bool hasScale = false;
    bool hasResolution = false;
    bool hasFit = false;
    QStringList positional;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (!argument.startsWith(QLatin1String("--import-"))) {
            positional.append(argument);
            continue;
        }
        sawInternal = true;
        if (argument == QLatin1String("--import-image")
            || argument == QLatin1String("--import-scale")
            || argument == QLatin1String("--import-resolution")
            || argument == QLatin1String("--import-fit")) {
            if (index + 1 >= arguments.size()
                || arguments.at(index + 1).startsWith(QLatin1Char('-'))) {
                if (error)
                    *error = QStringLiteral("%1 requires a separate value").arg(argument);
                return false;
            }
            const QString value = arguments.at(++index);
            if (argument == QLatin1String("--import-image")) {
                if (import->requested) {
                    if (error)
                        *error = QStringLiteral("--import-image may only be specified once");
                    return false;
                }
                import->requested = true;
                import->path = value;
            } else if (argument == QLatin1String("--import-scale")) {
                if (hasScale) {
                    if (error)
                        *error = QStringLiteral("--import-scale may only be specified once");
                    return false;
                }
                bool ok = false;
                import->scale = value.toInt(&ok);
                if (!ok || import->scale < 1) {
                    if (error)
                        *error = QStringLiteral("--import-scale must be a positive integer");
                    return false;
                }
                hasScale = true;
            } else if (argument == QLatin1String("--import-resolution")) {
                if (hasResolution) {
                    if (error)
                        *error = QStringLiteral("--import-resolution may only be specified once");
                    return false;
                }
                const QStringList parts = value.toLower().split(QLatin1Char('x'));
                bool widthOk = false;
                bool heightOk = false;
                import->width = parts.size() == 2 ? parts.at(0).toInt(&widthOk) : 0;
                import->height = parts.size() == 2 ? parts.at(1).toInt(&heightOk) : 0;
                if (!widthOk || !heightOk || import->width < 1 || import->height < 1
                    || import->width > omapixel::Document::maxDimension
                    || import->height > omapixel::Document::maxDimension) {
                    if (error)
                        *error = QStringLiteral(
                            "--import-resolution must be WIDTHxHEIGHT, with each side between 1 and %1")
                                     .arg(omapixel::Document::maxDimension);
                    return false;
                }
                hasResolution = true;
            } else {
                if (hasFit) {
                    if (error)
                        *error = QStringLiteral("--import-fit may only be specified once");
                    return false;
                }
                import->fit = value.toLower();
                if (import->fit != QLatin1String("contain")
                    && import->fit != QLatin1String("cover")
                    && import->fit != QLatin1String("stretch")) {
                    if (error)
                        *error = QStringLiteral(
                            "--import-fit must be contain, cover, or stretch");
                    return false;
                }
                hasFit = true;
            }
            continue;
        }
        if (error)
            *error = QStringLiteral("unknown internal import argument: %1").arg(argument);
        return false;
    }

    if (!sawInternal)
        return true;
    if (!import->requested) {
        if (error)
            *error = QStringLiteral("internal image import requires --import-image");
        return false;
    }
    if (!positional.isEmpty()) {
        if (error)
            *error = QStringLiteral("internal image import cannot open a document path");
        return false;
    }
    if (import->path.isEmpty()) {
        if (error)
            *error = QStringLiteral("--import-image requires a non-empty path");
        return false;
    }
    if (hasScale == hasResolution) {
        if (error)
            *error = QStringLiteral(
                "internal image import requires exactly one of --import-scale or --import-resolution");
        return false;
    }
    return true;
}

bool saveImageAtomically(const QImage &image, const QString &path,
                         const QStringList &sources, QString *error)
{
    QBuffer encoded;
    encoded.open(QIODevice::WriteOnly);
    if (!image.save(&encoded, "PNG")) {
        if (error)
            *error = QStringLiteral("could not encode PNG");
        return false;
    }
    return omapixel::output::writeAtomically(path, encoded.data(), sources, error);
}

} // namespace

int main(int argc, char *argv[])
{
    // Answered before QGuiApplication exists, because constructing it needs a
    // display and asking a program its version does not. A package build runs
    // on a machine with no screen at all.
    for (int i = 1; i < argc; ++i) {
        const QLatin1String argument(argv[i]);
        if (argument == QLatin1String("--version") || argument == QLatin1String("-v")) {
            std::printf("omapixel-studio %s\n", OMAPIXEL_VERSION);
            return 0;
        }
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omapixel"));
    app.setApplicationVersion(QStringLiteral(OMAPIXEL_VERSION));
    app.setApplicationDisplayName(QStringLiteral("omapixel"));
    app.setDesktopFileName(QStringLiteral("omapixel-studio"));

    InternalImageImport internalImport;
    QString internalImportError;
    if (!parseInternalImageImport(app.arguments(), &internalImport,
                                  &internalImportError)) {
        std::fprintf(stderr, "image import arguments failed: %s\n",
                     qPrintable(safeDiagnostic(internalImportError)));
        return 2;
    }

    // Registered by hand rather than through QML_ELEMENT. With qmake,
    // QML_ELEMENT needs a qmltypes step and an import name declared in the .pro,
    // and when it is missing the failure is `import omapixel` not resolving --
    // which surfaces as an empty window and no message at all.
    qmlRegisterType<omapixel::PixelGridItem>("omapixel", 1, 0, "PixelGridItem");
    qmlRegisterUncreatableType<omapixel::DocumentModel>(
        "omapixel", 1, 0, "DocumentModel",
        QStringLiteral("the document is owned by the application"));

    // Set OMAPIXEL_DEBUG_INPUT=1 to have the surface log every wheel event it
    // receives, with its deltas and modifiers. Input that never arrives and
    // input that arrives and is ignored look identical from the outside, and
    // guessing between them wastes more time than the switch costs.
    // The window's words, from i18n/<language>.json. English is always loaded
    // first, so a catalogue that only translates half of it shows English for
    // the other half instead of gaps.
    // The settings, and the keymap with them, from
    // ~/.config/omapixel/config.toml. Loaded before anything asks a question
    // of them -- the language the window is read in is one of the settings.
    omapixel::Config &config = omapixel::Config::shared();
    config.load();

    omapixel::Strings &strings = omapixel::Strings::shared();
    strings.load(omapixel::Strings::preferredLanguage());
    // Saving the file re-reads it, and a language changed there takes effect
    // with the rest. The window rebinds its keys on the same signal.
    QObject::connect(&config, &omapixel::Config::changed, &app, [&strings] {
        strings.load(omapixel::Strings::preferredLanguage());
    });

    omapixel::InputLog inputLog(qEnvironmentVariableIsSet("OMAPIXEL_DEBUG_INPUT"));

    omapixel::DocumentModel document;
    // Follows omarchy's active theme, and keeps following it: switching theme
    // while the window is open recolours it without a restart.
    omapixel::Theme theme;

    // What this studio holds, published for the command line's side of the
    // live loop: an agent can see a window is on the file -- and whether it
    // has unsaved work -- before it writes. Retired on the way out, so a
    // leftover file means a studio that did not leave cleanly.
    omapixel::SessionPublisher sessions;
    QObject::connect(&sessions, &omapixel::SessionPublisher::publicationFailed,
                     &document, [&document](const QString &error) {
                         std::fprintf(stderr, "session publication failed: %s\n",
                                       qPrintable(safeDiagnostic(error)));
                         document.say(error);
                     });
    sessions.follow(&document);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &sessions,
                     &omapixel::SessionPublisher::retire);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &document,
                     &omapixel::DocumentModel::retireScratch);

    // A file named on the command line, so `omapixel-studio drawing.json` works
    // the way every other editor does.
    const QStringList arguments = app.arguments();
    if (internalImport.requested) {
        if (!document.importImage(internalImport.path, QStringLiteral("document"),
                                  internalImport.scale, internalImport.width,
                                  internalImport.height, internalImport.fit,
                                  QStringLiteral("Imported"))) {
            std::fprintf(stderr, "image import failed: %s\n",
                         qPrintable(safeDiagnostic(document.note())));
            document.retireScratch();
            return 1;
        }
    } else if (arguments.size() > 1) {
        document.open(arguments.at(1));
    }

    QQmlApplicationEngine engine;
    // Exposed as a context property rather than instantiated from QML: there is
    // exactly one document per window, and letting QML make a second one would
    // be letting QML own the model.
    // OMAPIXEL_SHOT_SHEET=colour opens that panel before the screenshot is
    // taken. Popups do not appear in a window grab, so a panel that only
    // exists once opened cannot otherwise be looked at without a display.
    engine.rootContext()->setContextProperty(
        QStringLiteral("shotSheet"), QString::fromUtf8(qgetenv("OMAPIXEL_SHOT_SHEET")));
    engine.rootContext()->setContextProperty(QStringLiteral("T"), &strings);
    engine.rootContext()->setContextProperty(QStringLiteral("cfg"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("log"), &inputLog);
    engine.rootContext()->setContextProperty(QStringLiteral("doc"), &document);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    // A QML file that fails to load has to say so. The default is an empty
    // window and a bare exit code, which is the least debuggable failure there
    // is.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [](const QUrl &url) {
                         qCritical("could not load %s", qPrintable(url.toString()));
                         QCoreApplication::exit(1);
                     },
                     Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    // OMAPIXEL_SHOT=<path> renders the window to a PNG and exits. The studio is
    // the one part of this project that could not be inspected without a
    // screen; a layout change had to be described and taken on trust. With this
    // it can be looked at from a terminal, on a machine with no display at all.
    const QByteArray shot = qgetenv("OMAPIXEL_SHOT");
    const QByteArray layerShot = qgetenv("OMAPIXEL_SHOT_LAYER");
    const QByteArray layerGeometry = qgetenv("OMAPIXEL_SHOT_LAYER_GEOMETRY");
    const QString followedSource = document.followedPath();
    const QStringList outputSources = followedSource.isEmpty()
                                           ? QStringList{}
                                           : QStringList{followedSource};
    if (!layerShot.isEmpty()) {
        QStringList screenshotOutputs{QString::fromUtf8(layerShot)};
        if (!shot.isEmpty())
            screenshotOutputs.append(QString::fromUtf8(shot));
        if (!layerGeometry.isEmpty())
            screenshotOutputs.append(QString::fromUtf8(layerGeometry));
        QString collectiveError;
        if (!omapixel::output::validateAll(screenshotOutputs, outputSources,
                                           &collectiveError)) {
            std::fprintf(stderr, "screenshot output failed: %s\n",
                         qPrintable(safeDiagnostic(collectiveError)));
            return 1;
        }
        for (QObject *root : engine.rootObjects()) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            if (!window)
                continue;
            QObject *toolObject = window->findChild<QObject *>(
                QStringLiteral("layerToolWindow"));
            if (!toolObject)
                continue;
            QMetaObject::invokeMethod(toolObject, "openFor",
                                      Q_ARG(QString, document.activeLayerId()));
            auto *toolWindow = qobject_cast<QQuickWindow *>(toolObject);
            if (!toolWindow)
                continue;
            QTimer::singleShot(600, window,
                               [window, toolWindow, layerShot, layerGeometry, shot,
                                outputSources] {
                const QImage studio = window->grabWindow();
                const QImage tool = toolWindow->grabWindow();
                QImage composite(studio.width() + tool.width() + 32,
                                 qMax(studio.height(), tool.height()),
                                 QImage::Format_ARGB32);
                composite.fill(Qt::black);
                QPainter painter(&composite);
                painter.drawImage(0, 0, studio);
                painter.drawImage(studio.width() + 32, 0, tool);
                painter.end();
                 QString outputError;
                 const bool wroteComposite = saveImageAtomically(
                     composite, QString::fromUtf8(layerShot), outputSources, &outputError);
                 bool wroteStudio = true;
                 if (!shot.isEmpty())
                     wroteStudio = saveImageAtomically(studio, QString::fromUtf8(shot),
                                                       outputSources, &outputError);
                 bool wroteGeometry = true;
                 if (!layerGeometry.isEmpty()) {
                     const QByteArray record = QString(
                             "{\"studio\":{\"x\":%1,\"y\":%2,\"width\":%3,\"height\":%4},"
                             "\"layerTool\":{\"x\":%5,\"y\":%6,\"width\":%7,\"height\":%8,"
                             "\"windowType\":\"top-level\",\"transientParent\":true}}\n")
                            .arg(window->x()).arg(window->y()).arg(window->width())
                            .arg(window->height()).arg(toolWindow->x()).arg(toolWindow->y())
                            .arg(toolWindow->width()).arg(toolWindow->height())
                             .toUtf8();
                     wroteGeometry = omapixel::output::writeAtomically(
                         QString::fromUtf8(layerGeometry), record, outputSources, &outputError);
                 }
                 if (!wroteComposite || !wroteStudio || !wroteGeometry)
                      std::fprintf(stderr, "screenshot output failed: %s\n",
                                   qPrintable(safeDiagnostic(outputError)));
                 std::fprintf(stderr, "%s %s + %s\n",
                             wroteComposite && wroteStudio && wroteGeometry
                                 ? "wrote"
                                 : "could not write",
                              qPrintable(safeDiagnostic(QString::fromUtf8(layerShot))),
                              qPrintable(safeDiagnostic(QString::fromUtf8(shot))));
                QCoreApplication::exit(wroteComposite && wroteStudio && wroteGeometry ? 0 : 1);
            });
            break;
        }
    } else if (!shot.isEmpty()) {
        for (QObject *root : engine.rootObjects()) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            if (!window)
                continue;
            QObject::connect(
                window, &QQuickWindow::afterRendering, &app,
                [window, shot, outputSources] {
                    // One frame late: the first pass has laid nothing out yet.
                    QTimer::singleShot(400, window, [window, shot, outputSources] {
                        const QImage image = window->grabWindow();
                         QString outputError;
                         const bool written = saveImageAtomically(
                             image, QString::fromUtf8(shot), outputSources, &outputError);
                         if (!written)
                              std::fprintf(stderr, "screenshot output failed: %s\n",
                                           qPrintable(safeDiagnostic(outputError)));
                         std::fprintf(stderr, "%s %s (%dx%d)\n",
                                      written ? "wrote" : "could not write",
                                      qPrintable(safeDiagnostic(QString::fromUtf8(shot))),
                                      image.width(), image.height());
                        QCoreApplication::exit(written ? 0 : 1);
                    });
                },
                Qt::SingleShotConnection);
        }
    }

    if (inputLog.enabled()) {
        std::fprintf(stderr, "omapixel: input logging on — scroll over the drawing\n");
        std::fflush(stderr);
        // On the window itself, so wheel events are seen on the way in whether
        // or not anything in QML ends up handling them.
        for (QObject *root : engine.rootObjects()) {
            if (auto *window = qobject_cast<QQuickWindow *>(root))
                window->installEventFilter(&inputLog);
        }
    }
    if (engine.rootObjects().isEmpty()) {
        qCritical("the studio has no window — see the QML errors above");
        return 1;
    }

    return app.exec();
}
