/*
    SPDX-FileCopyrightText: 2006-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// To time the creation and total launch time (i. e. until window is
// visible/responsive):
// #define PROFILE_STARTUP

// Own
#include "Application.h"
#include "MainWindow.h"
#include "ViewManager.h"
#include "config-konsole.h"
#include "widgets/ViewContainer.h"

#include <algorithm>

// OS specific
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLockFile>
#include <QProcessEnvironment>
#include <QProxyStyle>
#include <QStandardPaths>

// KDE
#include <KAboutData>
#include <KConfigGroup>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KWindowSystem>

#if HAVE_DBUS
#include <KDBusService>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#endif

#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif

using Konsole::Application;

#ifdef PROFILE_STARTUP
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#endif

// fill the KAboutData structure with information about contributors to Konsole.
void fillAboutData(KAboutData &aboutData);

// restore sessions saved by KDE.
void restoreSession(Application &app);

#if HAVE_DBUS
QString applicationDBusServiceName()
{
    QStringList domainParts = QCoreApplication::organizationDomain().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    std::reverse(domainParts.begin(), domainParts.end());
    domainParts.append(QCoreApplication::applicationName());
    return domainParts.join(QLatin1Char('.'));
}
#endif

#if HAVE_DBUS
// Workaround for a bug in KDBusService: https://bugs.kde.org/show_bug.cgi?id=355545
// It calls exit(), but the program can't exit before the QApplication is deleted:
// https://bugreports.qt.io/browse/QTBUG-48709
static bool needToDeleteQApplication = false;
void deleteQApplication()
{
    if (needToDeleteQApplication) {
        delete qApp;
    }
}
#endif

// This override resolves following problem: since some qt version if
// XDG_CURRENT_DESKTOP ≠ kde, then pressing and immediately releasing Alt
// key makes focus get stuck in QMenu.
// Upstream report: https://bugreports.qt.io/browse/QTBUG-77355
class MenuStyle : public QProxyStyle
{
public:
    MenuStyle(const QString &name)
        : QProxyStyle(name)
    {
    }

    int styleHint(const StyleHint stylehint, const QStyleOption *opt, const QWidget *widget, QStyleHintReturn *returnData) const override
    {
        return (stylehint == QStyle::SH_MenuBar_AltKeyNavigation) ? 0 : QProxyStyle::styleHint(stylehint, opt, widget, returnData);
    }
};

// Used to control migrating config entries.
// Increment when there are new keys to migrate.
static int CurrentConfigVersion = 1;

static void migrateRenamedConfigKeys()
{
    KSharedConfigPtr konsoleConfig = KSharedConfig::openConfig(QStringLiteral("kmuxrc"));
    KConfigGroup verGroup = konsoleConfig->group(QStringLiteral("General"));
    const int savedVersion = verGroup.readEntry<int>("ConfigVersion", 0);
    if (savedVersion < CurrentConfigVersion) {
        struct KeyInfo {
            const char *groupName;
            const char *oldKeyName;
            const char *newKeyName;
        };

        static const KeyInfo keys[] = {{"KonsoleWindow", "SaveGeometryOnExit", "RememberWindowSize"}};

        // Migrate renamed config keys
        for (const auto &[group, oldName, newName] : keys) {
            KConfigGroup cg = konsoleConfig->group(QLatin1String(group));
            if (cg.exists() && cg.hasKey(oldName)) {
                const bool value = cg.readEntry(oldName, false);
                cg.deleteEntry(oldName);
                cg.writeEntry(newName, value);
            }
        }

        // With 5.93 KColorSchemeManager from KConfigWidgets, handles the loading
        // and saving of the widget color scheme, and uses "ColorScheme" as the
        // entry name, so clean-up here
        KConfigGroup cg(konsoleConfig, QStringLiteral("UiSettings"));
        const QString schemeName = cg.readEntry("WindowColorScheme");
        cg.deleteEntry("WindowColorScheme");
        cg.writeEntry("ColorScheme", schemeName);

        verGroup.writeEntry("ConfigVersion", CurrentConfigVersion);
        konsoleConfig->sync();
    }
}

// ***
// Entry point into the Konsole terminal application.
// ***
int main(int argc, char *argv[])
{
#ifdef PROFILE_STARTUP
    QElapsedTimer timer;
    timer.start();
#endif

    /**
     * trigger initialisation of proper icon theme
     */
#if KICONTHEMES_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    KIconTheme::initTheme();
#endif

    auto app = new QApplication(argc, argv);
    app->setDesktopFileName(QStringLiteral("io.github.kmux_project.kmux"));

#if HAVE_STYLE_MANAGER
    /**
     * trigger initialisation of proper application style
     */
    KStyleManager::initStyle();
#else
    /**
     * For Windows and macOS: use Breeze if available
     * Of all tested styles that works the best for us
     */
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QApplication::setStyle(QStringLiteral("breeze"));
#endif
#endif

    // fix the alt key, ensure we keep the current selected style as base
    app->setStyle(new MenuStyle(app->style()->name()));

    migrateRenamedConfigKeys();

    app->setWindowIcon(QIcon::fromTheme(QStringLiteral("kmux"), QIcon::fromTheme(QStringLiteral("utilities-terminal"))));

    KLocalizedString::setApplicationDomain("konsole");

    KAboutData about(QStringLiteral("kmux"),
                     i18nc("@title", "Kmux"),
                     QStringLiteral(KONSOLE_VERSION),
                     i18nc("@title", "Project workspace terminal"),
                     KAboutLicense::GPL_V2,
                     i18nc("@info:credit", "© 1997–2026 The Konsole Developers; © 2026 Kmux contributors"),
                     QString(),
                     QStringLiteral("https://github.com/vityas-off/kmux"));
    about.setDesktopFileName(QStringLiteral("io.github.kmux_project.kmux"));
    fillAboutData(about);

    KAboutData::setApplicationData(about);

    KCrash::initialize();

    QSharedPointer<QCommandLineParser> parser(new QCommandLineParser);
    parser->setApplicationDescription(about.shortDescription());
    about.setupCommandLine(parser.data());

    QStringList args = app->arguments();
    QStringList customCommand = Application::getCustomCommand(args);

    Application::populateCommandLineParser(parser.data());

    parser->process(args);
    about.processCommandLine(parser.data());

    // Keep read-only commands in the invoking process so their output is
    // written to its stdout even when another Kmux instance is running.
    if (Application::processHelpArgs(*parser)) {
        delete app;
        return 0;
    }

#if HAVE_DBUS
    // on wayland: init token if we are launched by Kmux and have none
    if (KWindowSystem::isPlatformWayland() && qEnvironmentVariable("XDG_ACTIVATION_TOKEN").isEmpty() && QDBusConnection::sessionBus().interface()) {
        // can we ask Kmux for a token?
        const auto konsoleService = qEnvironmentVariable("KMUX_DBUS_SERVICE");
        const auto konsoleSession = qEnvironmentVariable("KMUX_DBUS_SESSION");
        const auto konsoleActivationCookie = qEnvironmentVariable("KMUX_DBUS_ACTIVATION_COOKIE");
        if (!konsoleService.isEmpty() && !konsoleSession.isEmpty() && !konsoleActivationCookie.isEmpty()) {
            // we ask the current shell session
            QDBusMessage m = QDBusMessage::createMethodCall(konsoleService,
                                                            konsoleSession,
                                                            QStringLiteral("io.github.kmux_project.kmux.Session"),
                                                            QStringLiteral("activationToken"));

            // use the cookie from the environment
            m.setArguments({konsoleActivationCookie});

            // get the token, if possible and export it to environment for later use
            const auto tokenAnswer = QDBusConnection::sessionBus().call(m);
            if (tokenAnswer.type() == QDBusMessage::ReplyMessage && !tokenAnswer.arguments().isEmpty()) {
                const auto token = tokenAnswer.arguments().first().toString();
                if (!token.isEmpty()) {
                    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
                }
            }
        }
    }

    // Keep the lock until the primary exports its request object. This makes
    // service discovery and environment delivery atomic for concurrent starts.
    const QString runtimeDirectory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile startupLock(QDir(runtimeDirectory).filePath(QStringLiteral("kmux-startup.lock")));
    if (!startupLock.tryLock(30000)) {
        qWarning() << "Unable to coordinate Kmux startup:" << startupLock.error();
        delete app;
        return 1;
    }

    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    const QString serviceName = applicationDBusServiceName();
    bool hasExistingService = false;
    if (QDBusConnectionInterface *interface = sessionBus.interface()) {
        const QDBusReply<bool> serviceRegistered = interface->isServiceRegistered(serviceName);
        hasExistingService = serviceRegistered.isValid() && serviceRegistered.value();
    }
    if (hasExistingService) {
        startupLock.unlock();
        QDBusMessage activation = QDBusMessage::createMethodCall(serviceName,
                                                                 QStringLiteral("/KmuxApplication"),
                                                                 QStringLiteral("io.github.kmux_project.kmux.Application"),
                                                                 QStringLiteral("requestActivation"));
        activation.setArguments({app->arguments().mid(1), QDir::currentPath(), QProcessEnvironment::systemEnvironment().toStringList()});
        const QDBusReply<int> reply = sessionBus.call(activation);
        if (reply.isValid()) {
            delete app;
            return reply.value();
        }

        qWarning() << "Unable to activate the existing Kmux instance:" << reply.error().message();
        delete app;
        return 1;
    }

    needToDeleteQApplication = true;
    atexit(deleteQApplication);
    // Project workspaces are persisted as one global workspace tree. Always
    // route subsequent launches into the existing process so a second window
    // cannot overwrite the saved project state on exit.
    KDBusService dbusService(KDBusService::Unique | KDBusService::NoExitOnFailure);

    needToDeleteQApplication = false;
#endif

    // If we reach this location, there was no existing copy of Kmux
    // running, so create a new instance.
    Application konsoleApp(parser, customCommand);

#if HAVE_DBUS
    if (!sessionBus.registerObject(QStringLiteral("/KmuxApplication"), &konsoleApp, QDBusConnection::ExportScriptableInvokables)) {
        qWarning() << "Unable to register the Kmux activation object on DBus:" << sessionBus.lastError().message();
    }
    startupLock.unlock();

    // The activateRequested() signal is emitted when a second instance
    // of Konsole is started.
    QObject::connect(&dbusService, &KDBusService::activateRequested, &konsoleApp, &Application::slotActivateRequested);
#endif

    if (app->isSessionRestored()) {
        restoreSession(konsoleApp);
    } else {
        // Do not finish starting Konsole due to:
        // 1. An argument was given to just printed info
        // 2. An invalid situation occurred
        const bool continueStarting = (konsoleApp.newInstance() != 0);
        if (!continueStarting) {
            delete app;
            return 0;
        }
    }

#ifdef PROFILE_STARTUP
    qDebug() << "Construction completed in" << timer.elapsed() << "ms";
    QTimer::singleShot(0, [&timer]() {
        qDebug() << "Startup complete in" << timer.elapsed() << "ms";
    });
#endif

    // Since we've allocated the QApplication on the heap for the KDBusService workaround,
    // we need to delete it manually before returning from main().
    int ret = app->exec();
    delete app;
    return ret;
}

void fillAboutData(KAboutData &aboutData)
{
    aboutData.setOrganizationDomain("github.com");

    aboutData.addAuthor(i18nc("@info:credit", "Kurt Hindenburg"),
                        i18nc("@info:credit",
                              "General maintainer, bug fixes and general"
                              " improvements"),
                        QStringLiteral("kurt.hindenburg@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Robert Knight"),
                        i18nc("@info:credit", "Previous maintainer, ported to KDE4"),
                        QStringLiteral("robertknight@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Lars Doelle"), i18nc("@info:credit", "Original author"), QStringLiteral("lars.doelle@on-line.de"));
    aboutData.addCredit(i18nc("@info:credit", "Ahmad Samir"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("a.samirh78@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Carlos Alves"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("cbc.alves@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Tomaz Canabrava"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("tcanabrava@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Gustavo Carneiro"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("gcarneiroa@hotmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Edwin Pujols"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("edwin.pujols5@outlook.com"));
    aboutData.addCredit(i18nc("@info:credit", "Martin T. H. Sandsmark"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("martin.sandsmark@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Nate Graham"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("nate@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Mariusz Glebocki"),
                        i18nc("@info:credit", "Bug fixes and major improvements"),
                        QStringLiteral("mglb@arccos-1.net"));
    aboutData.addCredit(i18nc("@info:credit", "Thomas Surrel"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("thomas.surrel@protonmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Jekyll Wu"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("adaptee@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Waldo Bastian"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("bastian@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Stephan Binner"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("binner@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Thomas Dreibholz"), i18nc("@info:credit", "General improvements"), QStringLiteral("dreibh@iem.uni-due.de"));
    aboutData.addCredit(i18nc("@info:credit", "Chris Machemer"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("machey@ceinetworks.com"));
    aboutData.addCredit(i18nc("@info:credit", "Francesco Cecconi"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("francesco.cecconi@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Stephan Kulow"), i18nc("@info:credit", "Solaris support and history"), QStringLiteral("coolo@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Alexander Neundorf"),
                        i18nc("@info:credit", "Bug fixes and improved startup performance"),
                        QStringLiteral("neundorf@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Peter Silva"), i18nc("@info:credit", "Marking improvements"), QStringLiteral("Peter.A.Silva@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Lotzi Boloni"),
                        i18nc("@info:credit",
                              "Embedded Konsole\n"
                              "Toolbar and session names"),
                        QStringLiteral("boloni@cs.purdue.edu"));
    aboutData.addCredit(i18nc("@info:credit", "David Faure"),
                        i18nc("@info:credit",
                              "Embedded Konsole\n"
                              "General improvements"),
                        QStringLiteral("faure@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Antonio Larrosa"), i18nc("@info:credit", "Visual effects"), QStringLiteral("larrosa@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Matthias Ettrich"),
                        i18nc("@info:credit",
                              "Code from the kvt project\n"
                              "General improvements"),
                        QStringLiteral("ettrich@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Warwick Allison"),
                        i18nc("@info:credit", "Schema and text selection improvements"),
                        QStringLiteral("warwick@troll.no"));
    aboutData.addCredit(i18nc("@info:credit", "Dan Pilone"), i18nc("@info:credit", "SGI port"), QStringLiteral("pilone@slac.com"));
    aboutData.addCredit(i18nc("@info:credit", "Kevin Street"), i18nc("@info:credit", "FreeBSD port"), QStringLiteral("street@iname.com"));
    aboutData.addCredit(i18nc("@info:credit", "Sven Fischer"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("herpes@kawo2.renditionwth-aachen.de"));
    aboutData.addCredit(i18nc("@info:credit", "Dale M. Flaven"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("dflaven@netport.com"));
    aboutData.addCredit(i18nc("@info:credit", "Martin Jones"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("mjones@powerup.com.au"));
    aboutData.addCredit(i18nc("@info:credit", "Lars Knoll"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("knoll@mpi-hd.mpg.de"));
    aboutData.addCredit(i18nc("@info:credit", "Thanks to many others.\n"));
}

void restoreSession(Application &app)
{
    int n = 1;

    if (!KMainWindow::canBeRestored(n)) {
        return;
    }

    auto mainWindow = app.newMainWindow();
    mainWindow->restore(n);
    mainWindow->viewManager()->toggleActionsBasedOnState();
    mainWindow->show();

    // Without visiting the restored tabs, the sessions remain uninitialized
    // and do not display the correct information.
    mainWindow->viewManager()->initializeRestoredSessions();
}
