/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ApplicationTest.h"

#include "../Application.h"
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
#include <QSharedPointer>
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

void ApplicationTest::testProfilePropertySkipsInitialWorkspaceRestore()
{
    KConfigGroup savedWorkspace(KSharedConfig::openStateConfig(), QStringLiteral("LastProjectWorkspaceState"));
    savedWorkspace.deleteGroup();
    {
        auto sourceWindow = MainWindow();
        sourceWindow.newTab();
        QVERIFY(QMetaObject::invokeMethod(sourceWindow.viewManager(), "createProject", Qt::DirectConnection));
        sourceWindow.viewManager()->saveSessions(savedWorkspace);
        savedWorkspace.sync();
    }

    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux"), QStringLiteral("-p"), QStringLiteral("TabColor=#ff336699")});
    Application application(parser, {});

    application.newInstance();

    auto *window = mainWindow();
    QVERIFY(window != nullptr);
    auto *projects = qobject_cast<ProjectWorkspaceContainer *>(window->viewManager()->widget());
    QVERIFY(projects != nullptr);
    QCOMPARE(projects->projectCount(), 1);
    QVERIFY(activeSession(window) != nullptr);
    QCOMPARE(activeSession(window)->color(), QColor(QStringLiteral("#ff336699")));

    delete window;
    savedWorkspace.deleteGroup();
    savedWorkspace.sync();
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
