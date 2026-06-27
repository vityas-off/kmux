/*
    SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ViewManagerTest.h"
#include <QFile>
#include <QTest>

#include "../MainWindow.h"
#include "../ViewManager.h"
#include "../containers/ContainerSessionState.h"
#include "../containers/IContainerDetector.h"
#include "../session/Session.h"
#include "../session/SessionController.h"
#include "../widgets/ProjectWorkspaceContainer.h"
#include "../widgets/ViewContainer.h"
#include <QStandardPaths>

using namespace Konsole;

namespace
{
class TestContainerDetector : public IContainerDetector
{
public:
    using IContainerDetector::IContainerDetector;

    QString typeId() const override
    {
        return QStringLiteral("distrobox");
    }

    QString displayName() const override
    {
        return QStringLiteral("Distrobox");
    }

    QString iconName() const override
    {
        return QStringLiteral("distrobox");
    }

    std::optional<ContainerInfo> detect(int) const override
    {
        return std::nullopt;
    }

    QStringList entryCommand(const QString &containerName) const override
    {
        return {QStringLiteral("distrobox"), QStringLiteral("enter"), containerName};
    }

    void startListContainers() override
    {
        Q_EMIT listContainersFinished({});
    }
};
}

void ViewManagerTest::initTestCase()
{
    m_testDir = new QTemporaryDir(QDir::tempPath() + QDir::separator() + QStringLiteral("konsoleviewmanagertest-XXXXXX"));
}

void ViewManagerTest::testSaveLayout()
{
    // Single tab:
    // - Horizontally split view, with one view that is vertically split
    // The numeric values mean the view number, which is not relevant for this test, since we create new views
    QStringList expectedHierarchy = {QStringLiteral("(0)[0|(1){1|2}]")};

    auto mw = MainWindow();
    mw.viewManager()->newSession(mw.viewManager()->defaultProfile(), m_testDir->path());
    mw.viewManager()->splitLeftRight();
    mw.viewManager()->splitTopBottom();

    mw.viewManager()->saveLayout(m_testDir->filePath(QStringLiteral("test.json")));
    QCOMPARE(mw.viewManager()->viewHierarchy(), expectedHierarchy);

    QFile layoutFile(m_testDir->filePath(QStringLiteral("test.json")));
    QVERIFY(layoutFile.exists());
}

void ViewManagerTest::testLoadLayout()
{
    // Two tabs:
    // - First tab: Has only single view. We expect the layout to be opened in new tab.
    // - Second tab: Horizontally split view, with one view that is vertically split
    // The numeric values mean the view number, which is not relevant for this test, since we create new views
    QStringList expectedHierarchy = {QStringLiteral("(2)[3]"), QStringLiteral("(3)[4|(4){5|6}]")};

    auto mw = MainWindow();
    mw.viewManager()->newSession(mw.viewManager()->defaultProfile(), m_testDir->path());

    QFile layoutFile(m_testDir->filePath(QStringLiteral("test.json")));
    QVERIFY(layoutFile.exists());

    mw.viewManager()->loadLayout(m_testDir->filePath(QStringLiteral("test.json")));
    QCOMPARE(mw.viewManager()->viewHierarchy(), expectedHierarchy);
}

void ViewManagerTest::testProjectWorkspacesKeepIndependentTabs()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);
    QCOMPARE(workspaces->projectCount(), 1);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    QCOMPARE(firstProject->count(), 1);

    QWidget *firstProjectInitialTab = firstProject->currentWidget();
    QVERIFY(firstProjectInitialTab != nullptr);

    mw.newTab();
    QCOMPARE(firstProject->count(), 2);
    firstProject->setCurrentIndex(0);
    QCOMPARE(firstProject->currentWidget(), firstProjectInitialTab);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QCOMPARE(workspaces->projectCount(), 2);
    QCOMPARE(firstProject->count(), 2);
    QCOMPARE(secondProject->count(), 1);

    mw.newTab();
    QCOMPARE(firstProject->count(), 2);
    QCOMPARE(secondProject->count(), 2);
    secondProject->setCurrentIndex(1);
    QWidget *secondProjectActiveTab = secondProject->currentWidget();
    QVERIFY(secondProjectActiveTab != nullptr);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentWidget(), firstProjectInitialTab);
    QCOMPARE(firstProject->count(), 2);

    workspaces->activateProject(secondProject);
    QCOMPARE(viewManager->activeContainer(), secondProject);
    QCOMPARE(secondProject->currentWidget(), secondProjectActiveTab);
    QCOMPARE(secondProject->count(), 2);
}

void ViewManagerTest::testSplitsStayInActiveProjectWorkspace()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    QCOMPARE(firstProject->currentTabViewCount(), 1);

    viewManager->splitLeftRight();
    QCOMPARE(firstProject->currentTabViewCount(), 2);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QCOMPARE(secondProject->currentTabViewCount(), 1);

    viewManager->splitLeftRight();
    QCOMPARE(secondProject->currentTabViewCount(), 2);
    QCOMPARE(firstProject->currentTabViewCount(), 2);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentTabViewCount(), 2);
    QCOMPARE(secondProject->currentTabViewCount(), 2);
}

void ViewManagerTest::testSessionCountUsesActiveProjectWorkspace()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    mw.newTab();
    QCOMPARE(firstProject->count(), 2);
    QCOMPARE(viewManager->sessionList().count(), 2);
    QCOMPARE(viewManager->sessionCount(), 2);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QCOMPARE(secondProject->count(), 1);
    QCOMPARE(viewManager->sessionList().count(), 1);
    QCOMPARE(viewManager->sessionCount(), 1);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(viewManager->sessionList().count(), 2);
    QCOMPARE(viewManager->sessionCount(), 2);
}

void ViewManagerTest::testContainerMenuLaunchKeepsPendingColor()
{
    auto mw = MainWindow();

    TestContainerDetector detector;
    ContainerInfo container;
    container.detector = &detector;
    container.name = QStringLiteral("codex");
    container.displayName = QStringLiteral("codex");
    container.iconName = QStringLiteral("distrobox");

    const bool invoked = QMetaObject::invokeMethod(&mw, "newInContainer", Qt::DirectConnection, Q_ARG(Konsole::ContainerInfo, container));
    QVERIFY(invoked);

    auto *controller = mw.viewManager()->activeViewController();
    QVERIFY(controller != nullptr);
    Session *session = controller->session();
    QVERIFY(session != nullptr);

    const QString key = QStringLiteral("%1:%2").arg(detector.typeId(), container.name);
    QCOMPARE(session->property(ContainerSessionState::PendingContainerKeyProperty).toString(), key);
    QCOMPARE(session->color(), ContainerSessionState::colorForContainerKey(key));

    // Simulate transient host-side process state before in-container shell is confirmed.
    session->setContainerContext(ContainerInfo{});
    QCOMPARE(session->property(ContainerSessionState::PendingContainerKeyProperty).toString(), key);
    QCOMPARE(session->color(), ContainerSessionState::colorForContainerKey(key));

    // Once container detection confirms context, pending state is cleared.
    session->setContainerContext(container);
    QCOMPARE(session->property(ContainerSessionState::PendingContainerKeyProperty).toString(), QString());
    QCOMPARE(session->color(), ContainerSessionState::colorForContainerKey(key));
}

QTEST_MAIN(ViewManagerTest)

#include "moc_ViewManagerTest.cpp"
