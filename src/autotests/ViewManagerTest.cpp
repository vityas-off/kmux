/*
    SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ViewManagerTest.h"
#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>

#include "../MainWindow.h"
#include "../ViewManager.h"
#include "../containers/ContainerSessionState.h"
#include "../containers/IContainerDetector.h"
#include "../session/Session.h"
#include "../session/SessionController.h"
#include "../terminalDisplay/TerminalDisplay.h"
#include "../widgets/ProjectWorkspaceContainer.h"
#include "../widgets/ViewContainer.h"
#include "../widgets/ViewSplitter.h"
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

void ViewManagerTest::testSessionsIncludesAllProjectWorkspaces()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    mw.newTab();

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    QCOMPARE(viewManager->sessionList().count(), 1);
    QCOMPARE(viewManager->viewProperties().count(), 1);
    QList<Session *> sessions = viewManager->sessions();
    QCOMPARE(QSet<Session *>(sessions.begin(), sessions.end()).count(), 3);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->sessionList().count(), 2);
    QCOMPARE(viewManager->viewProperties().count(), 2);
    sessions = viewManager->sessions();
    QCOMPARE(QSet<Session *>(sessions.begin(), sessions.end()).count(), 3);
}

void ViewManagerTest::testProjectWorkspaceSummaryTracksActiveTab()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    QVERIFY(firstProject->activeViewSplitter() != nullptr);
    QVERIFY(firstProject->activeViewSplitter()->activeTerminalDisplay() != nullptr);
    firstProject->activeViewSplitter()->activeTerminalDisplay()->sessionController()->session()->setTitle(Session::DisplayedTitleRole,
                                                                                                          QStringLiteral("first-tab"));
    viewManager->refreshProjectSummary(firstProject);

    QCOMPARE(workspaces->projectTabCount(firstProject), 1);
    QVERIFY(workspaces->projectSubtitle(firstProject).contains(QStringLiteral("first-tab")));

    mw.newTab();
    firstProject->setCurrentIndex(1);
    QVERIFY(firstProject->activeViewSplitter() != nullptr);
    QVERIFY(firstProject->activeViewSplitter()->activeTerminalDisplay() != nullptr);
    firstProject->activeViewSplitter()->activeTerminalDisplay()->sessionController()->session()->setTitle(Session::DisplayedTitleRole,
                                                                                                          QStringLiteral("second-tab"));
    viewManager->refreshProjectSummary(firstProject);

    QCOMPARE(workspaces->projectTabCount(firstProject), 2);
    QVERIFY(workspaces->projectSubtitle(firstProject).contains(QStringLiteral("second-tab")));

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    QCOMPARE(workspaces->projectTabCount(firstProject), 2);
    QCOMPARE(workspaces->projectTabCount(secondProject), 1);
}

void ViewManagerTest::testProjectWorkspaceTerminalNotificationMarksInactiveProject()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *firstTerminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));

    Q_EMIT firstSession->terminalNotificationReceived(QStringLiteral("Codex"), QStringLiteral("Turn complete"));
    QVERIFY(workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectNotification(firstProject), QStringLiteral("Codex: Turn complete"));

    workspaces->activateProject(firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectNotification(firstProject), QStringLiteral("Codex: Turn complete"));
}

void ViewManagerTest::testProjectWorkspaceStatusTracksSessionHooks()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *firstTerminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::None);

    firstSession->setProjectStatus(QStringLiteral("needsInput"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    QVERIFY(workspaces->projectHasActivity(firstProject));

    workspaces->activateProject(firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    firstSession->setProjectStatus(QStringLiteral("running"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Running);

    firstSession->setProjectStatus(QStringLiteral("idle"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Idle);

    firstSession->setProjectStatus(QStringLiteral("unknown"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::None);
}

void ViewManagerTest::testProjectWorkspaceNavigationShortcuts()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    QWidget *firstProjectInitialTab = firstProject->currentWidget();
    QVERIFY(firstProjectInitialTab != nullptr);
    mw.newTab();
    firstProject->setCurrentIndex(0);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    viewManager->createProject();
    auto *thirdProject = viewManager->activeContainer();
    QVERIFY(thirdProject != nullptr);
    QVERIFY(thirdProject != firstProject);
    QVERIFY(thirdProject != secondProject);

    auto *previousWorkspace = mw.actionCollection()->action(QStringLiteral("previous-workspace"));
    QVERIFY(previousWorkspace != nullptr);
    QCOMPARE(previousWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageUp));

    auto *addWorkspace = mw.actionCollection()->action(QStringLiteral("add-workspace"));
    QVERIFY(addWorkspace != nullptr);
    QCOMPARE(addWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W));

    auto *nextWorkspace = mw.actionCollection()->action(QStringLiteral("next-workspace"));
    QVERIFY(nextWorkspace != nullptr);
    QCOMPARE(nextWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageDown));

    auto *switchToFirstWorkspace = mw.actionCollection()->action(QStringLiteral("switch-to-workspace-0"));
    QVERIFY(switchToFirstWorkspace != nullptr);
    QCOMPARE(switchToFirstWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1));

    auto *nextAttentionWorkspace = mw.actionCollection()->action(QStringLiteral("next-attention-workspace"));
    QVERIFY(nextAttentionWorkspace != nullptr);
    QCOMPARE(nextAttentionWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));

    nextWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentWidget(), firstProjectInitialTab);

    nextWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), secondProject);

    previousWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), firstProject);

    previousWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), thirdProject);

    switchToFirstWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentWidget(), firstProjectInitialTab);

    auto *thirdTerminal = thirdProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(thirdTerminal != nullptr);
    Session *thirdSession = thirdTerminal->sessionController()->session();
    QVERIFY(thirdSession != nullptr);
    thirdSession->setProjectStatus(QStringLiteral("needsInput"));

    nextAttentionWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), thirdProject);

    auto *firstTerminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);
    Q_EMIT firstSession->terminalNotificationReceived(QStringLiteral("Codex"), QStringLiteral("Turn complete"));

    nextAttentionWorkspace->trigger();
    QCOMPARE(viewManager->activeContainer(), firstProject);
}

void ViewManagerTest::testProjectWorkspaceDetachActionsDisabled()
{
    auto mw = MainWindow();

    auto *detachTab = mw.actionCollection()->action(QStringLiteral("detach-tab"));
    QVERIFY(detachTab != nullptr);
    QVERIFY(!detachTab->isEnabled());
    QVERIFY(!detachTab->isVisible());
    QVERIFY(detachTab->shortcut().isEmpty());

    auto *detachView = mw.actionCollection()->action(QStringLiteral("detach-view"));
    QVERIFY(detachView != nullptr);
    QVERIFY(!detachView->isEnabled());
    QVERIFY(!detachView->isVisible());
    QVERIFY(detachView->shortcut().isEmpty());
}

void ViewManagerTest::testProjectWorkspaceNewWindowActionDisabled()
{
    auto mw = MainWindow();

    auto *newWindow = mw.actionCollection()->action(QStringLiteral("new-window"));
    QVERIFY(newWindow != nullptr);
    QVERIFY(!newWindow->isEnabled());
    QVERIFY(!newWindow->isVisible());
    QVERIFY(newWindow->shortcut().isEmpty());
}

void ViewManagerTest::testMoveTabBetweenProjectWorkspaces()
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

    auto *movedSplitter = firstProject->viewSplitterAt(0);
    QVERIFY(movedSplitter != nullptr);
    auto *movedTerminal = movedSplitter->activeTerminalDisplay();
    QVERIFY(movedTerminal != nullptr);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QCOMPARE(secondProject->count(), 1);

    viewManager->moveTabToProject(firstProject, 0, secondProject);

    QCOMPARE(workspaces->projectCount(), 2);
    QCOMPARE(firstProject->count(), 1);
    QCOMPARE(secondProject->count(), 2);
    QCOMPARE(viewManager->activeContainer(), secondProject);
    QCOMPARE(secondProject->currentWidget(), movedSplitter);
    QCOMPARE(viewManager->containerForTerminal(movedTerminal), secondProject);
}

void ViewManagerTest::testSaveSessionsStoresProjectWorkspaces()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    mw.newTab();
    firstProject->setCurrentIndex(1);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    mw.newTab();
    mw.newTab();
    secondProject->setCurrentIndex(2);

    KConfig config(m_testDir->filePath(QStringLiteral("workspaces-state-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    viewManager->saveSessions(group);

    const auto projectsDocument = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]")));
    const auto projects = projectsDocument.array();
    QCOMPARE(projects.count(), 2);
    QCOMPARE(group.readEntry("ActiveProject", -1), 1);

    const auto firstProjectObject = projects.at(0).toObject();
    QCOMPARE(firstProjectObject[QStringLiteral("Title")].toString(), QStringLiteral("Workspace 1"));
    QCOMPARE(firstProjectObject[QStringLiteral("Tabs")].toArray().count(), 2);
    QCOMPARE(firstProjectObject[QStringLiteral("Active")].toInt(), 1);

    const auto secondProjectObject = projects.at(1).toObject();
    QCOMPARE(secondProjectObject[QStringLiteral("Title")].toString(), QStringLiteral("Workspace 2"));
    QCOMPARE(secondProjectObject[QStringLiteral("Tabs")].toArray().count(), 3);
    QCOMPARE(secondProjectObject[QStringLiteral("Active")].toInt(), 2);

    const auto legacyTabs = QJsonDocument::fromJson(group.readEntry("Tabs", QByteArray("[]"))).array();
    QCOMPARE(legacyTabs.count(), 3);
    QCOMPARE(group.readEntry("Active", -1), 2);
}

void ViewManagerTest::testProjectWorkspaceRailWidthPersists()
{
    KConfig config(m_testDir->filePath(QStringLiteral("workspaces-width-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    {
        auto sourceWindow = MainWindow();
        auto *sourceManager = sourceWindow.viewManager();
        auto *sourceWorkspaces = sourceManager->_workspaceContainer.data();
        QVERIFY(sourceWorkspaces != nullptr);

        sourceWorkspaces->setProjectRailWidth(248);
        QCOMPARE(sourceWorkspaces->projectRailWidth(), 248);
        sourceWindow.newTab();
        sourceManager->saveSessions(group);
    }

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    auto *restoredWorkspaces = restoredManager->_workspaceContainer.data();
    QVERIFY(restoredWorkspaces != nullptr);

    restoredManager->restoreSessions(group, false);
    QCOMPARE(restoredWorkspaces->projectRailWidth(), 248);

    group.writeEntry("ProjectRailWidth", 999);
    restoredWorkspaces->setProjectRailWidth(164);
    restoredManager->restoreSessions(group, false);
    QCOMPARE(restoredWorkspaces->projectRailWidth(), 320);
}

void ViewManagerTest::testRestoreSessionsCreatesProjectWorkspacesWithoutSessionIds()
{
    KConfig config(m_testDir->filePath(QStringLiteral("workspaces-restore-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    {
        auto sourceWindow = MainWindow();
        auto *sourceManager = sourceWindow.viewManager();

        sourceWindow.newTab();
        auto *firstProject = sourceManager->activeContainer();
        QVERIFY(firstProject != nullptr);
        sourceWindow.newTab();
        firstProject->setCurrentIndex(1);

        sourceManager->createProject();
        auto *secondProject = sourceManager->activeContainer();
        QVERIFY(secondProject != nullptr);
        QVERIFY(secondProject != firstProject);
        sourceWindow.newTab();
        secondProject->setCurrentIndex(1);

        sourceManager->saveSessions(group);
    }

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    auto *restoredWorkspaces = restoredManager->_workspaceContainer.data();
    QVERIFY(restoredWorkspaces != nullptr);

    restoredManager->restoreSessions(group, false);

    QCOMPARE(restoredWorkspaces->projectCount(), 2);
    const auto restoredProjects = restoredWorkspaces->containers();
    QCOMPARE(restoredProjects.count(), 2);
    QCOMPARE(restoredProjects.at(0)->count(), 2);
    QCOMPARE(restoredProjects.at(0)->currentIndex(), 1);
    QCOMPARE(restoredProjects.at(1)->count(), 2);
    QCOMPARE(restoredProjects.at(1)->currentIndex(), 1);
    QCOMPARE(restoredManager->activeContainer(), restoredProjects.at(1));
    QCOMPARE(restoredManager->sessionList().count(), 2);
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
