// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Ranch: corral KDE Plasma desktop icons into titled, resizable groups
// ("corrals") that can also dock to a screen edge as slide-out drawers.
//
// Architecture in one paragraph (DESIGN.md has the full story): for every screen
// we create two transparent, full-screen wlr-layer-shell surfaces. The "pasture" sits
// on the BOTTOM layer, above the Plasma desktop and below windows, and holds free
// corrals. The "drawers" surface sits on the TOP layer, above windows, and holds
// docked corrals. Each surface's input region is shrunk to just the corrals/tabs,
// so everything else clicks straight through. Each corral is a real folder
// (~/Desktop/.ranch/<id>), so grouping an icon is just moving its file.
//
// Because corral files are hidden while we run, the desktop must be made whole again
// whenever we stop. That's DesktopStash: files are collected into corrals at start-up
// and parked back on the desktop on quit, logout (SIGTERM) and, via a guard process,
// crashes.
//
// This file wires it together: application metadata, single-instance handling,
// the stash lifecycle, the C++ types QML needs, and loading Main.qml.

#include "desktopstash.h"
#include "foldermodel.h"
#include "groupstore.h"
#include "pagemodel.h"
#include "ranch.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KSignalHandler>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include <csignal>
#include <cstdio>
#include <unistd.h>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    // Guard mode: `theranch --guard <pid>`, spawned by the app below. Handled before
    // anything else, so the guard never registers on D-Bus, never loads QML, and
    // stays a tiny idle process. (See DesktopStash::runGuard.)
    if (argc == 3 && qstrcmp(argv[1], "--guard") == 0) {
        QCoreApplication guardApp(argc, argv);
        return DesktopStash::runGuard(QByteArray(argv[2]).toLongLong());
    }

    // QApplication (not QGuiApplication): we use QWidgets for the native file menu,
    // the colour picker and KIO's dialogs. Layer-shell is picked up automatically by
    // QtWayland ≥ 6.5 when LayerShellQt is used on a window; no setup call is needed.
    QApplication app(argc, argv);
    // We live in the tray; hiding every corral must not quit the app.
    app.setQuitOnLastWindowClosed(false);
    KLocalizedString::setApplicationDomain("theranch");

    KAboutData about(u"theranch"_s,
                     i18n("The Ranch"),
                     QStringLiteral(THERANCH_VERSION), // from project() in CMakeLists.txt
                     i18n("Corral your desktop icons into titled, resizable groups and edge drawers"),
                     KAboutLicense::GPL_V3);
    // Must match the installed org.theranch.desktop, or the desktop portal can't
    // identify us ("App info not found").
    // GPL v3 *or later*, matching LICENSE and every file's SPDX header (the
    // constructor's GPL_V3 alone would mean "version 3 only").
    about.setLicense(KAboutLicense::GPL_V3, KAboutLicense::OrLaterVersions);
    about.setCopyrightStatement(i18n("© 2026 Tony Ayre"));
    about.addAuthor(i18n("Tony Ayre"), i18n("Author"), u"me@tonyayre.net"_s);
    // Without this KDE's default sends bug reports to bugs.kde.org, which isn't ours.
    about.setBugAddress("https://github.com/localzuk/TheRanch/issues");
    about.setDesktopFileName(u"org.theranch"_s);
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(u"view-grid-symbolic"_s));

    QCommandLineParser parser;
    const QCommandLineOption newCorral(u"new-corral"_s, i18n("Start drawing a new corral."));
    parser.addOption(newCorral);
    const QCommandLineOption restoreDesktop(u"restore-desktop"_s,
                                            i18n("Put every corral's icons back on the desktop and quit."));
    parser.addOption(restoreDesktop);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // Single instance. A second launch exits here and forwards its arguments to the
    // running one (handled by activateRequested below), so the menu entry's
    // "New Corral" action and `theranch --new-corral` work while we're running.
    KDBusService service(KDBusService::Unique);

    // We're the only instance. If we were only asked to restore the desktop, do it
    // and leave. (With an instance already running, the flag is forwarded to it
    // instead; it quits, and quitting parks.)
    if (parser.isSet(restoreDesktop)) {
        const int n = DesktopStash::park();
        fprintf(stdout, "%s\n", qPrintable(i18np("Returned %1 item to the desktop.", "Returned %1 items to the desktop.", n)));
        return 0;
    }

    // Follow the Plasma theme unless the user forced a style via QT_QUICK_CONTROLS_STYLE.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(u"org.kde.desktop"_s);
    }

    auto *store = new GroupStore(&app);

    // --- desktop stash lifecycle --------------------------------------------
    // 1. Pull last session's parked files back into their corrals. Must happen
    //    before QML creates the corrals' folder listings.
    QStringList ids;
    for (const Corral &c : store->corrals()) {
        ids << c.id;
    }
    DesktopStash::collect(ids);

    // 2. Start the guard: a detached copy of us that waits for this process to die
    //    and parks the files if we couldn't (crash, SIGKILL, failed start-up).
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            {u"--guard"_s, QString::number(qint64(::getpid()))});

    // 3. Every orderly exit parks: tray Quit, `--restore-desktop`, and the signals a
    //    logout or `kill` sends. Qt doesn't turn SIGTERM into a quit by itself, so
    //    without this the process would just die and leave the work to the guard.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        DesktopStash::park();
    });
    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [](int) {
        QCoreApplication::quit();
    });
    // -------------------------------------------------------------------------

    auto *ranch = new Ranch(store, &app);

    QObject::connect(&service, &KDBusService::activateRequested, ranch, [ranch](const QStringList &args, const QString &) {
        if (args.contains(u"--restore-desktop"_s)) {
            QCoreApplication::quit(); // aboutToQuit parks
        } else if (args.contains(u"--new-corral"_s)) {
            ranch->setDrawMode(true);
        } else {
            ranch->setAllHidden(false);
        }
    });

    // C++ types live in their own module (org.theranch.core), separate from the
    // QML-only module org.theranch that CMake's qt_add_qml_module builds.
    qmlRegisterSingletonInstance("org.theranch.core", 1, 0, "Ranch", ranch);
    qmlRegisterSingletonInstance("org.theranch.core", 1, 0, "Corrals", store);
    qmlRegisterType<FolderModel>("org.theranch.core", 1, 0, "FolderModel");
    qmlRegisterType<PageModel>("org.theranch.core", 1, 0, "PageModel");

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine); // makes i18n() available in QML
    engine.loadFromModule("org.theranch", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    // Only after QML has loaded, so the pasture windows exist to show the draw overlay.
    if (parser.isSet(newCorral)) {
        ranch->setDrawMode(true);
    }
    return app.exec();
}
