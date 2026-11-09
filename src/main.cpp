// OmaMastery: a mastery gradebook and planning hub that teachers shape with
// their AI agent. Its data is plain files in ~/Classes.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "setupflow.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    // Commands for agents and OmaClass run without a display.
    if (Cli::handles(arguments)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("omamastery");
        return Cli::run(arguments);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("omamastery");
    app.setApplicationVersion("0.2.1");

    // Matches the window to omamastery.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("omamastery");
    app.setWindowIcon(QIcon::fromTheme("omamastery"));

    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend(qEnvironmentVariable("OMAMASTERY_HOME"));
    // `omamastery open ap-csp` opens that class instead of the current one,
    // which is how OmaClass's Mastery button lands on the class it shows.
    // The one-time setup link, omamastery://setup?canvas=school.instructure.com,
    // arrives as `omamastery open <link>` from the desktop file.
    SetupFlow setup(qEnvironmentVariable("OMAMASTERY_HOME"));
    QObject::connect(&setup, &SetupFlow::finished, &backend, &Backend::reload);
    QString link;
    for (const QString &argument : arguments.mid(1))
        if (argument.startsWith("omamastery:"))
            link = argument;
    if (!link.isEmpty())
        setup.start(link);
    else if (arguments.value(1) == "open" && !arguments.value(2).isEmpty())
        backend.selectClassId(arguments.value(2));

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("setup", &setup);
    engine.rootContext()->setContextProperty("startInSetup", !link.isEmpty());
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Developer aid: OMAMASTERY_GRAB=out.png renders the window (offscreen is
    // fine) to an image and quits, so layout can be checked without a desktop.
    // OMAMASTERY_TAB picks the tab (0 Today, 1 Mastery, 2 Plan).
    const QString grab = qEnvironmentVariable("OMAMASTERY_GRAB");
    if (!grab.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!qEnvironmentVariable("OMAMASTERY_TAB").isEmpty())
            window->setProperty("tab", qEnvironmentVariable("OMAMASTERY_TAB").toInt());
        // OMAMASTERY_GRAB_STATE="contentX,contentY,row,col": a scrolled grid with a selected cell.
        const QStringList state = qEnvironmentVariable("OMAMASTERY_GRAB_STATE").split(',', Qt::SkipEmptyParts);
        QTimer::singleShot(600, &app, [window, state]() {
            QObject *grid = window->findChild<QObject *>("masteryGrid");
            QObject *view = window->findChild<QObject *>("masteryView");
            if (state.size() == 4 && grid && view) {
                view->setProperty("selRow", state[2].toInt());
                view->setProperty("selCol", state[3].toInt());
                grid->setProperty("contentX", state[0].toDouble());
                grid->setProperty("contentY", state[1].toDouble());
            }
        });
        QTimer::singleShot(1200, &app, [window, grab]() {
            window->grabWindow().save(grab);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
