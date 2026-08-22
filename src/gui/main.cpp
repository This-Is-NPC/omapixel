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
#include "Strings.h"
#include "PixelGridItem.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omapixel"));
    app.setApplicationDisplayName(QStringLiteral("omapixel"));
    app.setDesktopFileName(QStringLiteral("omapixel-studio"));

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

    // A file named on the command line, so `omapixel-studio drawing.json` works
    // the way every other editor does.
    const QStringList arguments = app.arguments();
    if (arguments.size() > 1)
        document.open(arguments.at(1));

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
    if (!shot.isEmpty()) {
        for (QObject *root : engine.rootObjects()) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            if (!window)
                continue;
            QObject::connect(
                window, &QQuickWindow::afterRendering, &app,
                [window, shot] {
                    // One frame late: the first pass has laid nothing out yet.
                    QTimer::singleShot(400, window, [window, shot] {
                        const QImage image = window->grabWindow();
                        const bool written = image.save(QString::fromUtf8(shot));
                        std::fprintf(stderr, "%s %s (%dx%d)\n",
                                     written ? "wrote" : "could not write",
                                     shot.constData(), image.width(), image.height());
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
