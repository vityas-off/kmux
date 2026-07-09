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
#include "../widgets/ViewContainer.h"
#include "../widgets/ViewSplitter.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QTest>

using namespace Konsole;

void ApplicationTest::testActivationUsesRequestWorkingDirectory()
{
    auto parser = QSharedPointer<QCommandLineParser>::create();
    Application::populateCommandLineParser(parser.get());
    parser->parse({QStringLiteral("kmux")});
    Application application(parser, {});

    QTemporaryDir callingDirectory;
    QVERIFY(callingDirectory.isValid());

    application.slotActivateRequested({QStringLiteral("--new-tab")}, callingDirectory.path());

    auto *window = qobject_cast<MainWindow *>(QApplication::activeWindow());
    if (window == nullptr) {
        const auto topLevelWidgets = QApplication::topLevelWidgets();
        for (QWidget *widget : topLevelWidgets) {
            if ((window = qobject_cast<MainWindow *>(widget)) != nullptr) {
                break;
            }
        }
    }
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

QTEST_MAIN(ApplicationTest)

#include "moc_ApplicationTest.cpp"
