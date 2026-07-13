/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ApplicationTest.h"

#include "../Application.h"
#include "../ApplicationMetadata.h"
#include "../MainWindow.h"
#include "../ViewManager.h"
#include "../session/Session.h"
#include "../session/SessionController.h"
#include "../terminalDisplay/TerminalDisplay.h"
#include "../widgets/ProjectWorkspaceContainer.h"
#include "../widgets/ViewContainer.h"
#include "../widgets/ViewSplitter.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <KConfigGroup>
#include <KSharedConfig>

using namespace Konsole;

namespace
{
MainWindow *mainWindow()
{
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevelWidgets) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            return window;
        }
    }
    return nullptr;
}

Session *activeSession(MainWindow *window)
{
    auto *splitter = window->viewManager()->activeContainer()->activeViewSplitter();
    return splitter != nullptr && splitter->activeTerminalDisplay() != nullptr ? splitter->activeTerminalDisplay()->sessionController()->session() : nullptr;
}

KConfigGroup savedWorkspaceGroup()
{
    return KConfigGroup(KSharedConfig::openStateConfig(), QStringLiteral("LastProjectWorkspaceState"));
}

void writeTwoProjectWorkspace()
{
    QJsonArray projects;
    projects.append(QJsonObject{{QStringLiteral("Title"), QStringLiteral("Saved One")}, {QStringLiteral("Tabs"), QJsonArray{}}, {QStringLiteral("Active"), 0}});
    projects.append(QJsonObject{{QStringLiteral("Title"), QStringLiteral("Saved Two")}, {QStringLiteral("Tabs"), QJsonArray{}}, {QStringLiteral("Active"), 0}});

    KConfigGroup group = savedWorkspaceGroup();
    group.writeEntry("Projects", QJsonDocument(projects).toJson(QJsonDocument::Compact));
    group.writeEntry("ActiveProject", 1);
    group.sync();
}
}

void ApplicationTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ApplicationTest::init()
{
    KConfigGroup group = savedWorkspaceGroup();
    group.deleteGroup();
    group.sync();
}

void ApplicationTest::cleanup()
{
    KConfigGroup group = savedWorkspaceGroup();
    group.deleteGroup();
    group.sync();
}

void ApplicationTest::testApplicationIdentity()
{
    QCOMPARE(ApplicationMetadata::componentName(), QStringLiteral("kmux"));
    QCOMPARE(ApplicationMetadata::organizationDomain(), QByteArrayLiteral("vityas_off.github.io"));
    QCOMPARE(ApplicationMetadata::desktopFileName(), QStringLiteral("io.github.vityas_off.kmux"));
    QCOMPARE(ApplicationMetadata::dbusServiceName(), ApplicationMetadata::desktopFileName());
    QCOMPARE(ApplicationMetadata::localServerName(), QStringLiteral("io.github.vityas_off.kmux.activation"));
}

void ApplicationTest::testInformationalArgumentsHandledLocally_data()
{
    QTest::addColumn<QString>("option");
    QTest::addColumn<bool>("handled");

    QTest::newRow("profiles") << QStringLiteral("--list-profiles") << true;
    QTest::newRow("profile-properties") << QStringLiteral("--list-profile-properties") << true;
    QTest::newRow("regular-launch") << QString() << false;
}

void ApplicationTest::testInformationalArgumentsHandledLocally()
{
    QFETCH(QString, option);
    QFETCH(bool, handled);

    QCommandLineParser parser;
    Application::populateCommandLineParser(&parser);
    QStringList args{QStringLiteral("kmux")};
    if (!option.isEmpty()) {
        args.append(option);
    }
    QVERIFY(parser.parse(args));

    QCOMPARE(Application::processHelpArgs(parser), handled);
}

void ApplicationTest::testActivationUsesRequestWorkingDirectory()
{
    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    QTemporaryDir callingDirectory;
    QVERIFY(callingDirectory.isValid());

    application.slotActivateRequested({QStringLiteral("--new-tab")}, callingDirectory.path());

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    auto *manager = window->viewManager();
    QVERIFY(manager->activeContainer()->activeViewSplitter() != nullptr);
    auto *terminal = manager->activeContainer()->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->sessionController()->session()->initialWorkingDirectory(), QDir::cleanPath(callingDirectory.path()));

    application.slotActivateRequested({QStringLiteral("--new-tab"), QStringLiteral("-e"), QStringLiteral("./script")}, callingDirectory.path());

    QVERIFY(manager->activeContainer()->activeViewSplitter() != nullptr);
    terminal = manager->activeContainer()->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *commandSession = terminal->sessionController()->session();
    QCOMPARE(commandSession->initialWorkingDirectory(), QDir::cleanPath(callingDirectory.path()));
    QCOMPARE(commandSession->program(), callingDirectory.filePath(QStringLiteral("script")));

    delete window;
}

void ApplicationTest::testActivationUsesRequestEnvironment()
{
    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    const QString inheritedEntry = QStringLiteral("KMUX_APPLICATION_TEST_CALLER=secondary");
    const QString profileEntry = QStringLiteral("KMUX_APPLICATION_TEST_CALLER=profile");
    const QString secretEntry = QStringLiteral("KMUX_APPLICATION_TEST_TOKEN=not-persisted");
    const QStringList environment{inheritedEntry, secretEntry};
    QCOMPARE(application.requestActivation({QStringLiteral("--new-tab"), QStringLiteral("-p"), QStringLiteral("Environment=%1").arg(profileEntry)},
                                           QDir::currentPath(),
                                           environment),
             0);

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    Session *session = activeSession(window);
    QVERIFY(session != nullptr);
    QVERIFY(session->hasProcessEnvironment());
    QCOMPARE(session->processEnvironment(), environment);
    QVERIFY(session->environment().contains(profileEntry));
    QVERIFY(!session->environment().contains(secretEntry));

    application.slotActivateRequested({QStringLiteral("--new-tab")}, QDir::currentPath());
    session = activeSession(window);
    QVERIFY(session != nullptr);
    QVERIFY(!session->hasProcessEnvironment());

    delete window;
}

void ApplicationTest::testProfileDirectoryPrecedence()
{
    QTemporaryDir profileDirectory;
    QTemporaryDir activationDirectory;
    QTemporaryDir explicitDirectory;
    QVERIFY(profileDirectory.isValid());
    QVERIFY(activationDirectory.isValid());
    QVERIFY(explicitDirectory.isValid());

    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    QVERIFY(parser->parse({QStringLiteral("kmux"), QStringLiteral("-p"), QStringLiteral("Directory=%1").arg(profileDirectory.path())}));
    Application application(parser, {});

    application.newInstance();

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->initialWorkingDirectory(), QDir::cleanPath(profileDirectory.path()));

    application.slotActivateRequested({QStringLiteral("--new-tab"), QStringLiteral("-p"), QStringLiteral("Directory=%1").arg(profileDirectory.path())},
                                      activationDirectory.path());
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->initialWorkingDirectory(), QDir::cleanPath(profileDirectory.path()));

    application.slotActivateRequested({QStringLiteral("--new-tab"),
                                       QStringLiteral("-p"),
                                       QStringLiteral("Directory=%1").arg(profileDirectory.path()),
                                       QStringLiteral("--workdir"),
                                       explicitDirectory.path()},
                                      activationDirectory.path());
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->initialWorkingDirectory(), QDir::cleanPath(explicitDirectory.path()));

    delete window;
}

void ApplicationTest::testActivationResolvesRelativeTabsFile()
{
    writeTwoProjectWorkspace();

    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    QTemporaryDir callingDirectory;
    QTemporaryDir profileDirectory;
    QVERIFY(callingDirectory.isValid());
    QVERIFY(profileDirectory.isValid());
    QFile profileFile(callingDirectory.filePath(QStringLiteral("application-test.profile")));
    QVERIFY(profileFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray profile = QStringLiteral("[General]\nName=ApplicationTestDirectory\nDirectory=%1\n").arg(profileDirectory.path()).toUtf8();
    QCOMPARE(profileFile.write(profile), profile.size());
    profileFile.close();

    QFile tabsFile(callingDirectory.filePath(QStringLiteral("tabs.conf")));
    QVERIFY(tabsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray tabs = QStringLiteral("profile: %1\n").arg(profileFile.fileName()).toUtf8();
    QCOMPARE(tabsFile.write(tabs), tabs.size());
    tabsFile.close();

    application.slotActivateRequested({QStringLiteral("--tabs-from-file"), QStringLiteral("tabs.conf")}, callingDirectory.path());

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    auto *projects = qobject_cast<ProjectWorkspaceContainer *>(window->viewManager()->widget());
    QVERIFY(projects != nullptr);
    QCOMPARE(projects->projectCount(), 2);
    QCOMPARE(projects->containers().at(0)->count(), 1);
    QCOMPARE(projects->containers().at(1)->count(), 2);
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->initialWorkingDirectory(), QDir::cleanPath(profileDirectory.path()));

    delete window;
}

void ApplicationTest::testActivationResolvesRelativeLayoutFile()
{
    writeTwoProjectWorkspace();

    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    QTemporaryDir callingDirectory;
    QVERIFY(callingDirectory.isValid());
    QFile layoutFile(callingDirectory.filePath(QStringLiteral("layout.json")));
    QVERIFY(layoutFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray layout = R"({
        "Orientation": "Horizontal",
        "Widgets": [{"SessionRestoreId": 0}]
    })";
    QCOMPARE(layoutFile.write(layout), layout.size());
    layoutFile.close();

    application.slotActivateRequested({QStringLiteral("--layout"), QStringLiteral("layout.json")}, callingDirectory.path());

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    auto *projects = qobject_cast<ProjectWorkspaceContainer *>(window->viewManager()->widget());
    QVERIFY(projects != nullptr);
    QCOMPARE(projects->projectCount(), 2);
    QCOMPARE(projects->containers().at(0)->count(), 1);
    QCOMPARE(projects->containers().at(1)->count(), 2);

    delete window;
}

void ApplicationTest::testExplicitSessionRequestPreservesInitialWorkspace_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::addColumn<QStringList>("customCommand");

    QTest::newRow("new-tab") << QStringList{QStringLiteral("kmux"), QStringLiteral("--new-tab")} << QStringList{};
    QTest::newRow("profile-property") << QStringList{QStringLiteral("kmux"), QStringLiteral("-p"), QStringLiteral("TabColor=#ff336699")} << QStringList{};
    QTest::newRow("workdir") << QStringList{QStringLiteral("kmux"), QStringLiteral("--workdir"), QDir::tempPath()} << QStringList{};
    QTest::newRow("profile") << QStringList{QStringLiteral("kmux"), QStringLiteral("--profile"), QStringLiteral("__missing_application_test_profile__")}
                             << QStringList{};
    QTest::newRow("builtin-profile") << QStringList{QStringLiteral("kmux"), QStringLiteral("--builtin-profile")} << QStringList{};
    QTest::newRow("hold") << QStringList{QStringLiteral("kmux"), QStringLiteral("--hold")} << QStringList{};
    QTest::newRow("background-mode") << QStringList{QStringLiteral("kmux"), QStringLiteral("--background-mode")} << QStringList{};
    QTest::newRow("command") << QStringList{QStringLiteral("kmux")} << QStringList{QStringLiteral("/bin/sh")};
}

void ApplicationTest::testExplicitSessionRequestPreservesInitialWorkspace()
{
    QFETCH(QStringList, arguments);
    QFETCH(QStringList, customCommand);

    writeTwoProjectWorkspace();

    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    QVERIFY(parser->parse(arguments));
    Application application(parser, customCommand);

    application.newInstance();

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    auto *projects = qobject_cast<ProjectWorkspaceContainer *>(window->viewManager()->widget());
    QVERIFY(projects != nullptr);
    QCOMPARE(projects->projectCount(), 2);
    QCOMPARE(projects->projectTitle(projects->containers().at(0)), QStringLiteral("Saved One"));
    QCOMPARE(projects->projectTitle(projects->containers().at(1)), QStringLiteral("Saved Two"));
    QCOMPARE(projects->containers().at(0)->count(), 1);
    QCOMPARE(projects->containers().at(1)->count(), 2);

    KConfigGroup savedWorkspace = savedWorkspaceGroup();
    window->viewManager()->saveSessions(savedWorkspace);
    savedWorkspace.sync();
    const QJsonArray savedProjects = QJsonDocument::fromJson(savedWorkspace.readEntry("Projects", QByteArray())).array();
    QCOMPARE(savedProjects.count(), 2);
    QCOMPARE(savedProjects.at(0).toObject()[QStringLiteral("Title")].toString(), QStringLiteral("Saved One"));
    QCOMPARE(savedProjects.at(1).toObject()[QStringLiteral("Title")].toString(), QStringLiteral("Saved Two"));

    delete window;
}

void ApplicationTest::testProfilePropertyCreatesTabOnActivation()
{
    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    application.slotActivateRequested({QStringLiteral("--new-tab")}, QDir::tempPath());
    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    QCOMPARE(window->viewManager()->activeContainer()->count(), 1);

    application.slotActivateRequested({QStringLiteral("-p"), QStringLiteral("TabColor=#ff663399")}, QDir::tempPath());

    QCOMPARE(window->viewManager()->activeContainer()->count(), 2);
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->color(), QColor(QStringLiteral("#ff663399")));

    delete window;
}

QTEST_MAIN(ApplicationTest)

#include "moc_ApplicationTest.cpp"
