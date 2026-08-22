// omapixel — the studio.
//
// A second front end over the same core the CLI drives. It owns the window and
// the input, and no rules: `DocumentModel` adapts types and calls `Document`,
// and `PixelGridItem` blits what `render::` produced. Anything either of them
// starts deciding on its own is a behaviour the CLI cannot reach and the tests
// do not cover.

#include "DocumentModel.h"
#include "InputLog.h"
#include "PixelGridItem.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
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
