/*
    SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ViewManagerTest.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KStandardAction>
#include <KXMLGUIFactory>

#include "../Emulation.h"
#include "../KonsoleSettings.h"
#include "../MainWindow.h"
#include "../ViewManager.h"
#include "../containers/ContainerSessionState.h"
#include "../containers/IContainerDetector.h"
#include "../profile/ProfileManager.h"
#include "../session/Session.h"
#include "../session/SessionController.h"
#include "../session/SessionManager.h"
#include "../terminalDisplay/TerminalDisplay.h"
#include "../widgets/ProjectWorkspaceContainer.h"
#include "../widgets/ViewContainer.h"
#include "../widgets/ViewSplitter.h"
#include "../workspaces/AgentSleepInhibitor.h"
#include <QStandardPaths>

#include <csignal>

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

void ViewManagerTest::testTabHistoryShortcutsStayInActiveProject()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *projects = manager->_workspaceContainer.data();
    QVERIFY(projects != nullptr);

    window.newTab();
    auto *firstProject = manager->activeContainer();
    QVERIFY(firstProject != nullptr);
    window.newTab();
    QCOMPARE(firstProject->count(), 2);

    auto *firstSplitter = firstProject->viewSplitterAt(0);
    auto *secondSplitter = firstProject->viewSplitterAt(1);
    QVERIFY(firstSplitter != nullptr);
    QVERIFY(secondSplitter != nullptr);
    auto *firstTerminal = firstSplitter->activeTerminalDisplay();
    auto *secondTerminal = secondSplitter->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    QVERIFY(secondTerminal != nullptr);

    manager->createProject();
    auto *backgroundProject = manager->activeContainer();
    QVERIFY(backgroundProject != nullptr);
    QVERIFY(backgroundProject != firstProject);
    auto *backgroundTerminal = backgroundProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(backgroundTerminal != nullptr);

    auto activateFirstTab = [&]() {
        projects->activateProject(firstProject);
        firstProject->setCurrentWidget(firstSplitter);
        firstTerminal->setFocus();
        QCOMPARE(manager->activeContainer(), firstProject);
        QCOMPARE(firstProject->currentWidget(), firstSplitter);
    };
    auto verifySecondTabActivated = [&]() {
        QCOMPARE(manager->activeContainer(), firstProject);
        QCOMPARE(firstProject->currentWidget(), secondSplitter);
    };

    auto *lastUsedAction = window.actionCollection()->action(QStringLiteral("last-used-tab"));
    auto *lastUsedReverseAction = window.actionCollection()->action(QStringLiteral("last-used-tab-reverse"));
    auto *toggleTwoTabsAction = window.actionCollection()->action(QStringLiteral("toggle-two-tabs"));
    QVERIFY(lastUsedAction != nullptr);
    QVERIFY(lastUsedReverseAction != nullptr);
    QVERIFY(toggleTwoTabsAction != nullptr);

    activateFirstTab();
    manager->_terminalDisplayHistory = {firstTerminal, backgroundTerminal, secondTerminal};
    manager->_terminalDisplayHistoryIndex = -1;
    lastUsedAction->trigger();
    verifySecondTabActivated();

    activateFirstTab();
    manager->_terminalDisplayHistory = {firstTerminal, secondTerminal, backgroundTerminal};
    manager->_terminalDisplayHistoryIndex = -1;
    lastUsedReverseAction->trigger();
    verifySecondTabActivated();

    activateFirstTab();
    manager->_terminalDisplayHistory = {firstTerminal, backgroundTerminal, secondTerminal};
    manager->_terminalDisplayHistoryIndex = -1;
    toggleTwoTabsAction->trigger();
    verifySecondTabActivated();
}

void ViewManagerTest::testFinishedBackgroundSessionIsRemovedFromTabHistory()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *projects = manager->_workspaceContainer.data();
    QVERIFY(projects != nullptr);

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, QStringLiteral("/bin/true"));
    profile->setProperty(Profile::Arguments, QStringList{QStringLiteral("/bin/true")});
    Session *backgroundSession = window.createSession(profile, m_testDir->path());
    QVERIFY(backgroundSession != nullptr);

    auto *backgroundProject = manager->activeContainer();
    auto *backgroundTerminal = backgroundProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(backgroundTerminal != nullptr);

    manager->createProject();
    auto *activeProject = manager->activeContainer();
    QVERIFY(activeProject != nullptr);
    QVERIFY(activeProject != backgroundProject);
    window.newTab();
    QCOMPARE(activeProject->count(), 2);
    QVERIFY(manager->_terminalDisplayHistory.contains(backgroundTerminal));

    QPointer<TerminalDisplay> deletedTerminal = backgroundTerminal;
    backgroundSession->run();
    QTRY_VERIFY(deletedTerminal.isNull());

    QVERIFY(!manager->_terminalDisplayHistory.contains(backgroundTerminal));
    manager->lastUsedView();
    QCOMPARE(manager->activeContainer(), activeProject);
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

void ViewManagerTest::testDbusLayoutOperationsRejectCrossProjectViews()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();

    window.newTab();
    auto *firstProject = manager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *firstProjectTerminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstProjectTerminal != nullptr);

    manager->createProject();
    auto *secondProject = manager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    auto *targetSplitter = secondProject->activeViewSplitter();
    QVERIFY(targetSplitter != nullptr);

    const QStringList firstProjectViewInfo{QStringLiteral("v-%1").arg(firstProjectTerminal->id())};
    QVERIFY(!manager->createSplitWithExisting(targetSplitter->id(), firstProjectViewInfo, 0, true));
    QVERIFY(!manager->moveView(firstProjectTerminal->id(), targetSplitter->id(), 0));

    QCOMPARE(manager->activeContainer(), secondProject);
    QCOMPARE(manager->containerForTerminal(firstProjectTerminal), firstProject);
    QCOMPARE(firstProject->currentTabViewCount(), 1);
    QCOMPARE(secondProject->currentTabViewCount(), 1);
}

void ViewManagerTest::testSessionCountIncludesAllProjectWorkspaces()
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
    QCOMPARE(viewManager->sessionList().count(), 3);
    QCOMPARE(viewManager->sessionCount(), 3);

    QSet<QString> expectedSessionIds;
    const QList<Session *> sessions = viewManager->sessions();
    for (const Session *session : sessions) {
        expectedSessionIds.insert(QString::number(session->sessionId()));
    }
    const QStringList sessionIds = viewManager->sessionList();
    QCOMPARE(QSet<QString>(sessionIds.begin(), sessionIds.end()), expectedSessionIds);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(viewManager->sessionList().count(), 3);
    QCOMPARE(viewManager->sessionCount(), 3);
    const QStringList sessionIdsAfterProjectSwitch = viewManager->sessionList();
    QCOMPARE(QSet<QString>(sessionIdsAfterProjectSwitch.begin(), sessionIdsAfterProjectSwitch.end()), expectedSessionIds);
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

    QCOMPARE(viewManager->sessionList().count(), 3);
    QCOMPARE(viewManager->viewProperties().count(), 1);
    QList<Session *> sessions = viewManager->sessions();
    QCOMPARE(QSet<Session *>(sessions.begin(), sessions.end()).count(), 3);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->sessionList().count(), 3);
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
    QWidget *notificationTab = firstProject->currentWidget();
    QVERIFY(notificationTab != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    mw.newTab();
    QVERIFY(firstProject->currentWidget() != notificationTab);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));

    Q_EMIT firstSession->terminalNotificationReceived(QStringLiteral("Codex"), QStringLiteral("Turn complete"));
    QVERIFY(workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectNotification(firstProject), QStringLiteral("Codex: Turn complete"));

    QSignalSpy activationSpy(viewManager, &ViewManager::activationRequest);
    firstTerminal->notificationClicked(QStringLiteral("notification-token"));

    QCOMPARE(activationSpy.count(), 1);
    QCOMPARE(activationSpy.constFirst().constFirst().toString(), QStringLiteral("notification-token"));
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentWidget(), notificationTab);
    QCOMPARE(firstProject->activeViewSplitter()->activeTerminalDisplay(), firstTerminal);
    QCOMPARE(viewManager->currentSession(), firstSession->sessionId());
    QVERIFY(!workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectNotification(firstProject), QStringLiteral("Codex: Turn complete"));
}

void ViewManagerTest::testProjectWorkspaceActivityClearsWhenTerminalRefocused()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    auto *controller = terminal->sessionController();
    QVERIFY(controller != nullptr);
    Session *session = controller->session();
    QVERIFY(session != nullptr);

    Q_EMIT session->emulation()->bell();
    viewManager->_sessionsNeedingAttention.insert(session);
    viewManager->refreshProjectSummary(project);
    QVERIFY(session->activeNotifications().testFlag(Session::Notification::Bell));
    QVERIFY(workspaces->projectHasActivity(project));

    Q_EMIT controller->viewFocused(controller);

    QCOMPARE(session->activeNotifications(), Session::NoNotification);
    QVERIFY(!viewManager->_sessionsNeedingAttention.contains(session));
    QVERIFY(!workspaces->projectHasActivity(project));
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
    QVERIFY(viewManager->hasProjectNeedingInput());

    workspaces->activateProject(firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    firstSession->setProjectStatus(QStringLiteral("running"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Running);
    QVERIFY(!viewManager->hasProjectNeedingInput());

    firstSession->setProjectStatus(QStringLiteral("idle"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Idle);

    firstSession->setProjectStatus(QStringLiteral("unknown"));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::None);
}

void ViewManagerTest::testTerminalTabsTrackSessionStatusesIndependently()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    const int firstTabIndex = project->currentIndex();
    auto *firstTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    mw.newTab();
    const int secondTabIndex = project->currentIndex();
    QVERIFY(secondTabIndex != firstTabIndex);
    auto *secondTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(secondTerminal != nullptr);
    Session *secondSession = secondTerminal->sessionController()->session();
    QVERIFY(secondSession != nullptr);
    QVERIFY(secondSession != firstSession);

    const qint64 firstBaseIcon = project->tabIcon(firstTabIndex).cacheKey();
    const qint64 secondBaseIcon = project->tabIcon(secondTabIndex).cacheKey();

    firstSession->setProjectStatus(QStringLiteral("needsInput"));
    secondSession->setProjectStatus(QStringLiteral("idle"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::NeedsInput);
    QCOMPARE(project->terminalTabStatus(secondTabIndex), TerminalTabStatus::AgentIdle);
    QVERIFY(!project->tabIcon(firstTabIndex).isNull());
    QVERIFY(!project->tabIcon(secondTabIndex).isNull());
    QVERIFY(project->tabIcon(firstTabIndex).cacheKey() != firstBaseIcon);
    QVERIFY(project->tabIcon(secondTabIndex).cacheKey() != secondBaseIcon);

    const QPixmap needsInputPixmap = project->tabIcon(firstTabIndex).pixmap(QSize(16, 16), 2.0);
    const QPixmap idlePixmap = project->tabIcon(secondTabIndex).pixmap(QSize(16, 16), 2.0);
    QCOMPARE(needsInputPixmap.size(), QSize(32, 32));
    QCOMPARE(idlePixmap.size(), QSize(32, 32));
    QCOMPARE(needsInputPixmap.toImage().pixelColor(0, 0).alpha(), 0);
    QCOMPARE(idlePixmap.toImage().pixelColor(0, 0).alpha(), 0);

    const qint64 needsInputIcon = project->tabIcon(firstTabIndex).cacheKey();
    project->updateNotification(firstTerminal->sessionController(), Session::Activity, true);
    QVERIFY(project->tabIcon(firstTabIndex).cacheKey() != needsInputIcon);
    project->setCurrentIndex(firstTabIndex);
    QCOMPARE(project->tabIcon(firstTabIndex).cacheKey(), needsInputIcon);
    project->setCurrentIndex(secondTabIndex);

    const qint64 idleIcon = project->tabIcon(secondTabIndex).cacheKey();
    secondSession->setProjectStatus(QStringLiteral("running"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::NeedsInput);
    QCOMPARE(project->terminalTabStatus(secondTabIndex), TerminalTabStatus::AgentRunning);
    QVERIFY(project->tabIcon(secondTabIndex).cacheKey() != idleIcon);

    firstSession->setProjectStatus(QStringLiteral("none"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::None);
    QCOMPARE(project->terminalTabStatus(secondTabIndex), TerminalTabStatus::AgentRunning);
    QCOMPARE(project->tabIcon(firstTabIndex).cacheKey(), firstBaseIcon);

    project->setCurrentIndex(firstTabIndex);
    viewManager->splitLeftRight();
    QCOMPARE(project->currentTabViewCount(), 2);
    const auto splitTerminals = project->viewSplitterAt(firstTabIndex)->findChildren<TerminalDisplay *>();
    QCOMPARE(splitTerminals.count(), 2);
    auto *splitTerminal = splitTerminals.at(0)->sessionController()->session() == firstSession ? splitTerminals.at(1) : splitTerminals.at(0);
    Session *splitSession = splitTerminal->sessionController()->session();
    QVERIFY(splitSession != nullptr);
    QVERIFY(splitSession != firstSession);

    splitSession->setProjectStatus(QStringLiteral("idle"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::AgentIdle);
    firstSession->setProjectStatus(QStringLiteral("rateLimited"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::RateLimited);
    splitSession->setProjectStatus(QStringLiteral("running"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::RateLimited);
    QCOMPARE(project->terminalTabStatus(secondTabIndex), TerminalTabStatus::AgentRunning);
    QCOMPARE(viewManager->_workspaceContainer->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::RateLimited);
    QVERIFY(!viewManager->hasProjectNeedingInput());
    firstSession->setProjectStatus(QStringLiteral("needsInput"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::NeedsInput);
    splitSession->setProjectStatus(QStringLiteral("rateLimited"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::NeedsInput);
    QCOMPARE(viewManager->_workspaceContainer->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    firstSession->setProjectStatus(QStringLiteral("idle"));
    QCOMPARE(project->terminalTabStatus(firstTabIndex), TerminalTabStatus::RateLimited);
}

void ViewManagerTest::testForegroundProcessAndIdleAgentUseDifferentStatuses()
{
#ifndef Q_OS_UNIX
    QSKIP("Foreground process checks are only available on Unix platforms.");
#else
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    const QString shell = QStandardPaths::findExecutable(QStringLiteral("bash"));
    QVERIFY(!shell.isEmpty());
    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, shell);
    profile->setProperty(Profile::Arguments, QStringList{shell, QStringLiteral("--noprofile"), QStringLiteral("--norc")});
    Session *session = mw.createSession(profile, m_testDir->path());
    QVERIFY(session != nullptr);
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    const int tabIndex = project->currentIndex();
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->sessionController()->session(), session);
    QSignalSpy outputSpy(session->emulation(), &Emulation::outputChanged);
    QVERIFY(outputSpy.isValid());
    session->run();
    QTRY_VERIFY(session->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!outputSpy.isEmpty(), 5000);
    QTRY_VERIFY(!session->isForegroundProcessActive());

    session->sendTextToTerminal(QStringLiteral("sleep 30"), QLatin1Char('\r'));
    QTRY_VERIFY(session->isForegroundProcessActive());
    viewManager->refreshProjectSummary(project);
    QCOMPARE(project->terminalTabStatus(tabIndex), TerminalTabStatus::ForegroundProcess);
    QCOMPARE(workspaces->projectActiveProcessCount(project), 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("SessionStart"), {}, {}, {});
    QCOMPARE(project->terminalTabStatus(tabIndex), TerminalTabStatus::AgentIdle);
    QCOMPARE(workspaces->projectActiveProcessCount(project), 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("none"), processId, QStringLiteral("claude"), QStringLiteral("SessionEnd"), {}, {}, {});
    QCOMPARE(project->terminalTabStatus(tabIndex), TerminalTabStatus::ForegroundProcess);
    QCOMPARE(workspaces->projectActiveProcessCount(project), 1);

    session->sendSignal(SIGINT);
    QTRY_VERIFY(!session->isForegroundProcessActive());
#endif
}

void ViewManagerTest::testRunningAgentsControlSleepInhibition()
{
    auto *inhibitor = AgentSleepInhibitor::instance();
    auto *settingItem = KonsoleSettings::self()->preventSleepWhileAgentsRunItem();
    QVERIFY(settingItem != nullptr);
    QVERIFY(settingItem->getDefault().toBool());

    const bool previousSetting = KonsoleSettings::preventSleepWhileAgentsRun();
    const auto restoreSetting = qScopeGuard([inhibitor, previousSetting] {
        KonsoleSettings::setPreventSleepWhileAgentsRun(previousSetting);
        inhibitor->updateInhibition();
    });
    KonsoleSettings::setPreventSleepWhileAgentsRun(true);
    inhibitor->updateInhibition();
    QVERIFY(!inhibitor->_inhibitionRequested);

    auto window = MainWindow();
    auto *viewManager = window.viewManager();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);

    window.newTab();
    auto *firstTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    window.newTab();
    auto *secondTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(secondTerminal != nullptr);
    Session *secondSession = secondTerminal->sessionController()->session();
    QVERIFY(secondSession != nullptr);
    QVERIFY(secondSession != firstSession);

    firstSession->setProjectStatus(QStringLiteral("needsInput"));
    QVERIFY(!inhibitor->_inhibitionRequested);

    secondSession->setProjectStatus(QStringLiteral("running"));
    QCOMPARE(viewManager->_workspaceContainer->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    QVERIFY(inhibitor->_inhibitionRequested);

    secondSession->setProjectStatus(QStringLiteral("rateLimited"));
    QVERIFY(!inhibitor->_inhibitionRequested);

    firstSession->setProjectStatus(QStringLiteral("running"));
    QVERIFY(inhibitor->_inhibitionRequested);

    auto secondWindow = MainWindow();
    secondWindow.newTab();
    auto *thirdController = secondWindow.viewManager()->activeViewController();
    QVERIFY(thirdController != nullptr);
    Session *thirdSession = thirdController->session();
    QVERIFY(thirdSession != nullptr);
    thirdSession->setProjectStatus(QStringLiteral("running"));
    firstSession->setProjectStatus(QStringLiteral("idle"));
    QVERIFY(inhibitor->_inhibitionRequested);

    thirdSession->setProjectStatus(QStringLiteral("idle"));
    QVERIFY(!inhibitor->_inhibitionRequested);

    thirdSession->setProjectStatus(QStringLiteral("running"));
    KonsoleSettings::setPreventSleepWhileAgentsRun(false);
    inhibitor->updateInhibition();
    QVERIFY(!inhibitor->_inhibitionRequested);
}

void ViewManagerTest::testProjectWorkspaceStatusClearsWhenAgentExits()
{
#ifndef Q_OS_UNIX
    QSKIP("Agent process liveness checks are only available on Unix platforms.");
#else
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    QProcess agentProcess;
    agentProcess.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(agentProcess.waitForStarted());

    session->setProjectStatusWithProcess(QStringLiteral("running"), agentProcess.processId());
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    viewManager->clearExitedSessionProjectStatuses();
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    agentProcess.kill();
    QVERIFY(agentProcess.waitForFinished());
    QTRY_COMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);
#endif
}

void ViewManagerTest::testProjectWorkspaceStatusClearsWhenAgentReturnsToShell()
{
#ifndef Q_OS_UNIX
    QSKIP("Foreground process checks are only available on Unix platforms.");
#else
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    const QString shell = QStandardPaths::findExecutable(QStringLiteral("bash"));
    QVERIFY(!shell.isEmpty());
    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setProperty(Profile::Command, shell);
    profile->setProperty(Profile::Arguments, QStringList{shell, QStringLiteral("--noprofile"), QStringLiteral("--norc")});
    Session *session = mw.createSession(profile, m_testDir->path());
    QVERIFY(session != nullptr);
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    QCOMPARE(terminal->sessionController()->session(), session);
    QSignalSpy outputSpy(session->emulation(), &Emulation::outputChanged);
    QVERIFY(outputSpy.isValid());
    session->run();
    QTRY_VERIFY(session->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!outputSpy.isEmpty(), 5000);
    QTRY_VERIFY(!session->isForegroundProcessActive());

    session->sendTextToTerminal(QStringLiteral("sleep 30"), QLatin1Char('\r'));
    QTRY_VERIFY(session->isForegroundProcessActive());
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), 0, QStringLiteral("claude"), QStringLiteral("Stop"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
    QVERIFY(viewManager->_sessionProjectStatuses.value(session).agentProcessWasForeground);

    session->sendSignal(SIGINT);
    QTRY_VERIFY(!session->isForegroundProcessActive());
    viewManager->clearExitedSessionProjectStatuses();
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);
#endif
}

void ViewManagerTest::testProjectWorkspaceAgentSessionDoesNotInheritAnotherAgentPid()
{
#ifndef Q_OS_UNIX
    QSKIP("Agent process liveness checks are only available on Unix platforms.");
#else
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    QProcess codexProcess;
    codexProcess.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(codexProcess.waitForStarted());
    session->setProjectStatusForAgentEvent(QStringLiteral("running"),
                                           codexProcess.processId(),
                                           QStringLiteral("codex"),
                                           QStringLiteral("SessionStart"),
                                           {},
                                           {},
                                           {});

    codexProcess.kill();
    QVERIFY(codexProcess.waitForFinished());
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), 0, QStringLiteral("claude"), QStringLiteral("SessionStart"), {}, {}, {});

    const auto claudeStatusWithoutPid = viewManager->_sessionProjectStatuses.value(session);
    QCOMPARE(claudeStatusWithoutPid.agent, QStringLiteral("claude"));
    QCOMPARE(claudeStatusWithoutPid.agentProcessId, 0);
    viewManager->clearExitedSessionProjectStatuses();
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    QProcess claudeProcess;
    claudeProcess.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    QVERIFY(claudeProcess.waitForStarted());
    session->setProjectStatusForAgentEvent(QStringLiteral("running"),
                                           claudeProcess.processId(),
                                           QStringLiteral("claude"),
                                           QStringLiteral("SessionStart"),
                                           {},
                                           {},
                                           {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentProcessId, claudeProcess.processId());

    claudeProcess.kill();
    QVERIFY(claudeProcess.waitForFinished());
    QTRY_COMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);
#endif
}

void ViewManagerTest::testProjectWorkspaceCodexDecisionKeysAreSessionScoped()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *firstTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *firstSession = firstTerminal->sessionController()->session();
    QVERIFY(firstSession != nullptr);

    mw.newTab();
    auto *secondTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(secondTerminal != nullptr);
    QVERIFY(secondTerminal != firstTerminal);
    Session *secondSession = secondTerminal->sessionController()->session();
    QVERIFY(secondSession != nullptr);
    QVERIFY(secondSession != firstSession);

    const qlonglong processId = QCoreApplication::applicationPid();
    firstSession
        ->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    secondSession
        ->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT firstTerminal->keyPressedSignal(&returnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(firstSession).pendingTerminalDecisions, 0);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(firstSession).status, ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(secondSession).status, ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    QKeyEvent escapeKey(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    Q_EMIT secondTerminal->keyPressedSignal(&escapeKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(secondSession).pendingTerminalDecisions, 0);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(secondSession).status, ProjectWorkspaceContainer::ProjectStatus::Idle);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    firstSession->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("codex"), QStringLiteral("PreToolUse"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    firstSession->setProjectStatus(QStringLiteral("needsInput"));
    Q_EMIT firstTerminal->keyPressedSignal(&returnKey);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
}

void ViewManagerTest::testProjectWorkspaceAgentInterruptClearsRunningStatus()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    for (const QString &agent : {QStringLiteral("codex"), QStringLiteral("claude")}) {
        session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, agent, QStringLiteral("UserPromptSubmit"), {}, {}, {});
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

        QTest::keyClick(terminal, Qt::Key_Escape, Qt::ShiftModifier);
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

        QTest::keyClick(terminal, Qt::Key_Escape);
        QCOMPARE(viewManager->_sessionProjectStatuses.value(session).status, ProjectWorkspaceContainer::ProjectStatus::Idle);
        QVERIFY(viewManager->_sessionProjectStatuses.value(session).turnInterrupted);
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

        session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, agent, QStringLiteral("PostToolUse"), {}, {}, {});
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);
        session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, agent, QStringLiteral("PermissionRequest"), {}, {}, {});
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

        session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, agent, QStringLiteral("UserPromptSubmit"), {}, {}, {});
        QVERIFY(!viewManager->_sessionProjectStatuses.value(session).turnInterrupted);
        QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
    }

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("other"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    QKeyEvent escapeKey(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&escapeKey);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
}

void ViewManagerTest::testProjectWorkspaceCodexAutoReviewedPermissionStaysRunning()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
}

void ViewManagerTest::testProjectWorkspaceClaudeDenialDoesNotOverrideStop()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("PermissionDenied"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("PostToolUse"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("Stop"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);
}

void ViewManagerTest::testProjectWorkspaceClaudeIdlePromptMarksInactiveProject()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *terminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("IdlePrompt"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Idle);
    QVERIFY(workspaces->projectHasActivity(firstProject));
    QVERIFY(!viewManager->hasProjectNeedingInput());

    workspaces->activateProject(firstProject);
    QVERIFY(!workspaces->projectHasActivity(firstProject));
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Idle);
}

void ViewManagerTest::testProjectWorkspaceClaudeIdlePromptClearsNotification()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("claude"), QStringLiteral("Notification"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("IdlePrompt"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);
}

void ViewManagerTest::testProjectWorkspaceClaudeIdlePromptPreservesPermissionRequest()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *terminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("claude"), QStringLiteral("PermissionRequest"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("IdlePrompt"), {}, {}, {});

    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
    QVERIFY(workspaces->projectHasActivity(firstProject));
    QVERIFY(viewManager->hasProjectNeedingInput());
}

void ViewManagerTest::testProjectWorkspaceClaudeIdlePromptKeepsBackgroundWorkRunning()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *firstProject = viewManager->activeContainer();
    QVERIFY(firstProject != nullptr);
    auto *terminal = firstProject->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("Stop"), {}, {}, {});
    QVERIFY(viewManager->_sessionProjectStatuses.value(session).claudeBackgroundWork);

    viewManager->createProject();
    auto *secondProject = viewManager->activeContainer();
    QVERIFY(secondProject != nullptr);
    QVERIFY(secondProject != firstProject);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("IdlePrompt"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(firstProject), ProjectWorkspaceContainer::ProjectStatus::Running);
    QVERIFY(!workspaces->projectHasActivity(firstProject));
}

void ViewManagerTest::testProjectWorkspaceClaudeRateLimitPersistsUntilResumed()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("Stop"), {}, {}, {});
    QVERIFY(viewManager->_sessionProjectStatuses.value(session).claudeBackgroundWork);

    viewManager->createProject();
    auto *otherProject = viewManager->activeContainer();
    Session *otherSession = otherProject->activeViewSplitter()->activeTerminalDisplay()->sessionController()->session();
    otherSession->setProjectStatus(QStringLiteral("running"));

    session->setProjectStatusForAgentEvent(QStringLiteral("rateLimited"), processId, QStringLiteral("claude"), QStringLiteral("RateLimit"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QVERIFY(!viewManager->_sessionProjectStatuses.value(session).claudeBackgroundWork);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::RateLimited);
    QCOMPARE(project->terminalTabStatus(0), TerminalTabStatus::RateLimited);
    QVERIFY(workspaces->projectHasActivity(project));
    QVERIFY(!viewManager->hasProjectNeedingInput());
    QCOMPARE(workspaces->projectStatus(otherProject), ProjectWorkspaceContainer::ProjectStatus::Running);

    workspaces->activateProject(project);
    QVERIFY(!workspaces->projectHasActivity(project));
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::RateLimited);

    QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&returnKey);
    QKeyEvent escapeKey(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&escapeKey);
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, QStringLiteral("claude"), QStringLiteral("IdlePrompt"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::RateLimited);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("PreToolUse"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
    QCOMPARE(project->terminalTabStatus(0), TerminalTabStatus::AgentRunning);

    session->setProjectStatusForAgentEvent(QStringLiteral("rateLimited"), processId, QStringLiteral("claude"), QStringLiteral("RateLimit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("UserPromptSubmit"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("rateLimited"), processId, QStringLiteral("claude"), QStringLiteral("RateLimit"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("none"), processId, QStringLiteral("claude"), QStringLiteral("SessionEnd"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);
    QCOMPARE(workspaces->projectStatus(otherProject), ProjectWorkspaceContainer::ProjectStatus::Running);
}

void ViewManagerTest::testProjectWorkspaceClaudeCompactionStartsNewPrompt()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    const QString claude = QStringLiteral("claude");
    const QString sessionId = QStringLiteral("session-1");
    const QString previousPromptId = QStringLiteral("prompt-1");
    const QString compactPromptId = QStringLiteral("prompt-2");

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("SessionStart"), sessionId, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), sessionId, previousPromptId, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("Stop"), sessionId, previousPromptId, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PreCompact"), sessionId, compactPromptId, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentPromptId, compactPromptId);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("PostCompact"), sessionId, previousPromptId, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("PostCompact"), sessionId, compactPromptId, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);
}

void ViewManagerTest::testProjectWorkspaceClaudeRejectsStaleHookIdentities()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    const QString claude = QStringLiteral("claude");
    const QString firstSession = QStringLiteral("session-1");
    const QString secondSession = QStringLiteral("session-2");
    const QString firstPrompt = QStringLiteral("prompt-1");
    const QString secondPrompt = QStringLiteral("prompt-2");

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("SessionStart"), firstSession, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), firstSession, firstPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("SessionStart"), secondSession, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentSessionId, secondSession);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentPromptId, QString());
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, claude, QStringLiteral("Notification"), firstSession, firstPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), secondSession, firstPrompt, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), secondSession, secondPrompt, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentPromptId, secondPrompt);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("Stop"), secondSession, firstPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"),
                                           processId,
                                           claude,
                                           QStringLiteral("PermissionRequest"),
                                           secondSession,
                                           secondPrompt,
                                           QStringLiteral("subagent-1"));
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("none"), processId, claude, QStringLiteral("SessionEnd"), firstSession, firstPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("none"), processId, claude, QStringLiteral("SessionEnd"), secondSession, secondPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::None);
}

void ViewManagerTest::testProjectWorkspaceClaudeTaskNotificationBeginsTurn()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    const QString claude = QStringLiteral("claude");
    const QString sessionId = QStringLiteral("session-1");
    const QString userPrompt = QStringLiteral("prompt-1");
    const QString firstNotificationPrompt = QStringLiteral("prompt-2");
    const QString secondNotificationPrompt = QStringLiteral("prompt-3");

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("SessionStart"), sessionId, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), sessionId, userPrompt, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("Stop"), sessionId, userPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
    QVERIFY(viewManager->_sessionProjectStatuses.value(session).claudeBackgroundWork);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PreToolUse"), sessionId, firstNotificationPrompt, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentPromptId, firstNotificationPrompt);
    QVERIFY(!viewManager->_sessionProjectStatuses.value(session).claudeBackgroundWork);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("Stop"), sessionId, firstNotificationPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PostToolUse"), sessionId, userPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("IdlePrompt"), sessionId, firstNotificationPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("Stop"), sessionId, secondNotificationPrompt, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).agentPromptId, secondNotificationPrompt);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PreToolUse"), sessionId, firstNotificationPrompt, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Idle);
}

void ViewManagerTest::testProjectWorkspaceClaudeSubagentResolutionClearsOnlyNotification()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    const QString claude = QStringLiteral("claude");
    const QString sessionId = QStringLiteral("session-1");
    const QString promptId = QStringLiteral("prompt-1");
    const QString subagentId = QStringLiteral("subagent-1");

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("SessionStart"), sessionId, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("UserPromptSubmit"), sessionId, promptId, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, claude, QStringLiteral("Notification"), sessionId, promptId, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"),
                                           processId,
                                           claude,
                                           QStringLiteral("PreToolUse"),
                                           sessionId,
                                           QStringLiteral("other-prompt"),
                                           subagentId);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PreToolUse"), sessionId, promptId, subagentId);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, claude, QStringLiteral("PermissionRequest"), sessionId, promptId, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, claude, QStringLiteral("Notification"), sessionId, promptId, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, claude, QStringLiteral("PermissionDenied"), sessionId, promptId, subagentId);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("idle"), processId, claude, QStringLiteral("IdlePrompt"), sessionId, promptId, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);
}

void ViewManagerTest::testProjectWorkspaceClaudeDecisionClearsOnTerminalInput()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("claude"), QStringLiteral("PermissionRequest"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("claude"), QStringLiteral("Notification"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&returnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("claude"), QStringLiteral("PreToolUse"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);

    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("claude"), QStringLiteral("Notification"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    Q_EMIT terminal->keyPressedSignal(&returnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
}

void ViewManagerTest::testProjectWorkspaceTracksMultipleCodexDecisionsInOneSession()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *terminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(terminal != nullptr);
    Session *session = terminal->sessionController()->session();
    QVERIFY(session != nullptr);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 2);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("codex"), QStringLiteral("PostToolUse"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 2);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    QKeyEvent firstReturnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&firstReturnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    QKeyEvent secondReturnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT terminal->keyPressedSignal(&secondReturnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::NeedsInput);

    session->setProjectStatusForAgentEvent(QStringLiteral("running"), processId, QStringLiteral("codex"), QStringLiteral("PostToolUse"), {}, {}, {});
    QCOMPARE(workspaces->projectStatus(project), ProjectWorkspaceContainer::ProjectStatus::Running);
}

void ViewManagerTest::testSessionSignalsAreHandledOnceAcrossMultipleViews()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    mw.newTab();
    auto *project = viewManager->activeContainer();
    QVERIFY(project != nullptr);
    auto *firstTerminal = project->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(firstTerminal != nullptr);
    Session *session = firstTerminal->sessionController()->session();
    QVERIFY(session != nullptr);

    auto *secondTerminal = viewManager->createView(session);
    project->addView(secondTerminal);
    QCOMPARE(session->views().count(), 2);

    const qlonglong processId = QCoreApplication::applicationPid();
    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);

    QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    Q_EMIT firstTerminal->keyPressedSignal(&returnKey);
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 0);

    QCOMPARE(viewManager->forgetTerminal(secondTerminal), session);
    QPointer<TerminalDisplay> deletedTerminal(secondTerminal);
    secondTerminal->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(deletedTerminal.isNull());
    QCOMPARE(session->views().count(), 1);

    session->setProjectStatusForAgentEvent(QStringLiteral("needsInput"), processId, QStringLiteral("codex"), QStringLiteral("PermissionRequest"), {}, {}, {});
    QCOMPARE(viewManager->_sessionProjectStatuses.value(session).pendingTerminalDecisions, 1);

    Q_EMIT session->terminalNotificationReceived(QStringLiteral("Codex"), QStringLiteral("Turn complete"));
    QCOMPARE(workspaces->projectNotification(project), QStringLiteral("Codex: Turn complete"));
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
    QCOMPARE(addWorkspace->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));

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

void ViewManagerTest::testProjectWorkspaceRailDoesNotAcceptFocus()
{
    ProjectWorkspaceContainer workspaces;
    auto *projectList = workspaces.findChild<QListWidget *>(QStringLiteral("projectList"));

    QVERIFY(projectList != nullptr);
    QCOMPARE(projectList->focusPolicy(), Qt::NoFocus);
}

void ViewManagerTest::testSelectedProjectFollowsRailBackground_data()
{
    // Palette roles produced by KColorScheme for Breeze Light and Breeze Dark.
    QTest::addColumn<QColor>("windowColor");
    QTest::addColumn<QColor>("baseColor");
    QTest::addColumn<QColor>("midColor");
    QTest::addColumn<QColor>("textColor");
    QTest::newRow("breeze-light") << QColor(239, 240, 241) << QColor(255, 255, 255) << QColor(196, 200, 204) << QColor(35, 38, 41);
    QTest::newRow("breeze-dark") << QColor(32, 35, 38) << QColor(20, 22, 24) << QColor(28, 31, 33) << QColor(252, 252, 252);
}

void ViewManagerTest::testSelectedProjectFollowsRailBackground()
{
    QFETCH(QColor, windowColor);
    QFETCH(QColor, baseColor);
    QFETCH(QColor, midColor);
    QFETCH(QColor, textColor);

    QPalette palette;
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::Base, baseColor);
    palette.setColor(QPalette::Mid, midColor);
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::WindowText, textColor);
    palette.setColor(QPalette::Highlight, QColor(61, 174, 233));

    // Apply the palette as a color scheme would, so the rail style sheet and the
    // project list pick it up as well.
    const QPalette previousPalette = QApplication::palette();
    QApplication::setPalette(palette);
    const auto restorePalette = qScopeGuard([&] {
        QApplication::setPalette(previousPalette);
    });

    auto window = MainWindow();
    window.resize(900, 600);
    auto *workspaces = window.viewManager()->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *projectList = workspaces->findChild<QListWidget *>(QStringLiteral("projectList"));
    QVERIFY(projectList != nullptr);
    QListWidgetItem *selectedItem = projectList->currentItem();
    QVERIFY(selectedItem != nullptr);
    QVERIFY(selectedItem->isSelected());

    // The selected project must stand out from the rail while keeping its text readable.
    const QRect itemRect = projectList->visualItemRect(selectedItem);
    const QImage image = projectList->viewport()->grab().toImage();
    const QColor background = image.pixelColor(itemRect.right() - 3, itemRect.center().y());
    QVERIFY2(qAbs(qGray(background.rgb()) - qGray(windowColor.rgb())) >= 10, qPrintable(background.name()));
    QVERIFY2(qAbs(qGray(background.rgb()) - qGray(textColor.rgb())) >= 150, qPrintable(background.name()));
}

void ViewManagerTest::testNoNavigationDisablesProjectActions()
{
    auto mw = MainWindow();
    auto *viewManager = mw.viewManager();
    auto *workspaces = viewManager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    viewManager->createProject();
    viewManager->createProject();
    QCOMPARE(workspaces->projectCount(), 3);

    viewManager->setNavigationMethod(ViewManager::NoNavigation);

    const QStringList actionNames = {
        QStringLiteral("add-workspace"),
        QStringLiteral("next-workspace"),
        QStringLiteral("previous-workspace"),
        QStringLiteral("next-attention-workspace"),
        QStringLiteral("switch-to-workspace-0"),
        QStringLiteral("switch-to-workspace-8"),
    };
    for (const QString &actionName : actionNames) {
        auto *action = mw.actionCollection()->action(actionName);
        QVERIFY(action != nullptr);
        QVERIFY(!action->isEnabled());
    }

    viewManager->createProject();
    QCOMPARE(workspaces->projectCount(), 3);
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

void ViewManagerTest::testUnsupportedHelpActionsHidden()
{
    const QString applicationName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("kmux"));
    const auto restoreApplicationName = qScopeGuard([applicationName] {
        QCoreApplication::setApplicationName(applicationName);
    });

    auto mw = MainWindow();
    auto *helpMenu = qobject_cast<QMenu *>(mw.factory()->container(QStringLiteral("help"), &mw));
    QVERIFY(helpMenu != nullptr);

    const auto helpAction = [helpMenu](KStandardAction::StandardAction standardAction) {
        const QString actionName = KStandardAction::name(standardAction);
        const auto actions = helpMenu->actions();
        for (QAction *action : actions) {
            if (action->objectName() == actionName) {
                return action;
            }
        }
        return static_cast<QAction *>(nullptr);
    };

    for (const auto standardAction : {
             KStandardAction::HelpContents,
             KStandardAction::WhatsThis,
             KStandardAction::AboutKDE,
         }) {
        auto *action = helpAction(standardAction);
        QVERIFY(action != nullptr);
        QVERIFY(!action->isVisible());
    }

    if (auto *reportBugAction = helpAction(KStandardAction::ReportBug)) {
        QVERIFY(!reportBugAction->isVisible());
    }
    if (auto *donateAction = helpAction(KStandardAction::Donate)) {
        QVERIFY(donateAction->isVisible());
    }

    auto *aboutApplicationAction = helpAction(KStandardAction::AboutApp);
    QVERIFY(aboutApplicationAction != nullptr);
    QVERIFY(aboutApplicationAction->isVisible());
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
    mw.newTab();
    QCOMPARE(firstProject->count(), 3);

    auto *sourceCurrentSplitter = firstProject->viewSplitterAt(2);
    QVERIFY(sourceCurrentSplitter != nullptr);
    QCOMPARE(firstProject->currentWidget(), sourceCurrentSplitter);

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
    QCOMPARE(firstProject->count(), 2);
    QCOMPARE(secondProject->count(), 2);
    QCOMPARE(viewManager->activeContainer(), secondProject);
    QCOMPARE(secondProject->currentWidget(), movedSplitter);
    QCOMPARE(viewManager->containerForTerminal(movedTerminal), secondProject);

    workspaces->activateProject(firstProject);
    QCOMPARE(viewManager->activeContainer(), firstProject);
    QCOMPARE(firstProject->currentWidget(), sourceCurrentSplitter);
    workspaces->activateProject(secondProject);

    auto *remainingSplitter = firstProject->viewSplitterAt(0);
    QVERIFY(remainingSplitter != nullptr);
    QVERIFY(!remainingSplitter->terminalMaximized());
    QVERIFY(!movedSplitter->terminalMaximized());

    Q_EMIT movedTerminal->requestToggleExpansion();

    QVERIFY(!remainingSplitter->terminalMaximized());
    QVERIFY(movedSplitter->terminalMaximized());
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
    QCOMPARE(firstProjectObject[QStringLiteral("Title")].toString(), QStringLiteral("Project 1"));
    QCOMPARE(firstProjectObject[QStringLiteral("Tabs")].toArray().count(), 2);
    QCOMPARE(firstProjectObject[QStringLiteral("Active")].toInt(), 1);

    const auto secondProjectObject = projects.at(1).toObject();
    QCOMPARE(secondProjectObject[QStringLiteral("Title")].toString(), QStringLiteral("Project 2"));
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

void ViewManagerTest::testProjectIconsPersistWithoutLoadingInactiveProjects()
{
    KConfig config(m_testDir->filePath(QStringLiteral("project-icons-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    const QString customPath = m_testDir->filePath(QStringLiteral("project-icon.png"));
    QPixmap custom(24, 24);
    custom.fill(Qt::red);
    QVERIFY(custom.save(customPath));
    const QStringList icons = {QStringLiteral(":/project-icons/material/code.svg"),
                               customPath,
                               QStringLiteral("folder"),
                               QStringLiteral(":/project-icons/devicon/python.svg"),
                               QStringLiteral(":/project-icons/devicon/monochrome/rust.svg")};

    {
        auto sourceWindow = MainWindow();
        auto *manager = sourceWindow.viewManager();
        auto *workspaces = manager->_workspaceContainer.data();
        sourceWindow.newTab();
        for (int i = 0; i < icons.size(); ++i) {
            if (i > 0) {
                manager->createProject();
            }
            workspaces->setProjectIconName(manager->activeContainer(), icons.at(i));
            sourceWindow.newTab();
            QCOMPARE(workspaces->projectIconName(manager->activeContainer()), icons.at(i));
        }
        manager->saveSessions(group);
    }

    auto restoredWindow = MainWindow();
    auto *manager = restoredWindow.viewManager();
    auto *workspaces = manager->_workspaceContainer.data();
    manager->restoreSessions(group, false);
    const auto projects = workspaces->containers();
    QCOMPARE(projects.size(), icons.size());
    QCOMPARE(projects.at(0)->count(), 0);
    QCOMPARE(projects.at(1)->count(), 0);
    auto *list = workspaces->findChild<QListWidget *>(QStringLiteral("projectList"));
    QVERIFY(list != nullptr);
    for (int i = 0; i < icons.size(); ++i) {
        QCOMPARE(workspaces->projectIconName(projects.at(i)), icons.at(i));
        QVERIFY(!list->item(i)->icon().pixmap(20).isNull());
    }
    QCOMPARE(list->item(1)->icon().pixmap(20).toImage().pixelColor(10, 10), QColor(Qt::red));

    manager->saveSessions(group);
    QCOMPARE(projects.at(0)->count(), 0);
    const auto savedProjects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    for (int i = 0; i < icons.size(); ++i) {
        QCOMPARE(savedProjects.at(i).toObject()[QStringLiteral("Icon")].toString(), icons.at(i));
        workspaces->activateProject(projects.at(i));
        QCOMPARE(workspaces->projectIconName(projects.at(i)), icons.at(i));
    }

    workspaces->setProjectIconName(projects.at(0), {});
    manager->saveSessions(group);
    const auto resetProjects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    QVERIFY(resetProjects.at(0).toObject()[QStringLiteral("Icon")].toString().isEmpty());
    QCOMPARE(workspaces->projectIconName(projects.at(1)), customPath);
}

void ViewManagerTest::testRestoreSessionsLazilyCreatesProjectWorkspacesWithoutSessionIds()
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

    const QString cachedDirectory = m_testDir->path();
    QJsonArray savedProjects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    QJsonObject firstSavedProject = savedProjects.at(0).toObject();
    firstSavedProject.insert(QStringLiteral("LastDirectory"), cachedDirectory);
    savedProjects[0] = firstSavedProject;
    group.writeEntry("Projects", QJsonDocument(savedProjects).toJson(QJsonDocument::Compact));

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    auto *restoredWorkspaces = restoredManager->_workspaceContainer.data();
    QVERIFY(restoredWorkspaces != nullptr);

    restoredManager->restoreSessions(group, false);

    QCOMPARE(restoredWorkspaces->projectCount(), 2);
    const auto restoredProjects = restoredWorkspaces->containers();
    QCOMPARE(restoredProjects.count(), 2);
    QCOMPARE(restoredProjects.at(0)->count(), 0);
    QCOMPARE(restoredWorkspaces->projectTabCount(restoredProjects.at(0)), 2);
    QCOMPARE(restoredWorkspaces->projectSubtitle(restoredProjects.at(0)), cachedDirectory);
    QVERIFY(!restoredWorkspaces->projectIsLoaded(restoredProjects.at(0)));
    QCOMPARE(restoredProjects.at(1)->count(), 2);
    QCOMPARE(restoredProjects.at(1)->currentIndex(), 1);
    QCOMPARE(restoredManager->activeContainer(), restoredProjects.at(1));
    QCOMPARE(restoredManager->sessionList().count(), 2);
    QVERIFY(restoredWorkspaces->projectIsLoaded(restoredProjects.at(1)));

    restoredWorkspaces->activateProject(restoredProjects.at(0));
    QCOMPARE(restoredProjects.at(0)->count(), 2);
    QCOMPARE(restoredProjects.at(0)->currentIndex(), 1);
    QCOMPARE(restoredManager->sessionList().count(), 4);
    QVERIFY(restoredWorkspaces->projectIsLoaded(restoredProjects.at(0)));

    restoredWorkspaces->activateProject(restoredProjects.at(1));
    QCOMPARE(restoredProjects.at(1)->currentIndex(), 1);
    restoredWorkspaces->activateProject(restoredProjects.at(0));
    QCOMPARE(restoredProjects.at(0)->count(), 2);
    QCOMPARE(restoredManager->sessionList().count(), 4);
}

void ViewManagerTest::testRestoredTerminalActionsStayInProject_data()
{
    QTest::addColumn<bool>("emptyProject");
    QTest::newRow("nested-splits") << false;
    QTest::newRow("empty-project") << true;
}

void ViewManagerTest::testRestoredTerminalActionsStayInProject()
{
    QFETCH(bool, emptyProject);

    KConfig config(m_testDir->filePath(QStringLiteral("restored-terminal-actions-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    {
        auto sourceWindow = MainWindow();
        sourceWindow.newTab();
        auto *sourceManager = sourceWindow.viewManager();
        sourceManager->createProject();
        sourceManager->splitLeftRight();
        sourceManager->splitTopBottom();
        sourceManager->saveSessions(group);
    }
    QCOMPARE(group.readEntry("ActiveProject", -1), 1);
    if (emptyProject) {
        auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
        auto project = projects.at(1).toObject();
        project[QStringLiteral("Tabs")] = QJsonArray{};
        projects[1] = project;
        group.writeEntry("Projects", QJsonDocument(projects).toJson(QJsonDocument::Compact));
    }

    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *workspaces = manager->_workspaceContainer.data();
    manager->restoreSessions(group, false);

    QCOMPARE(workspaces->projectCount(), 2);
    auto *firstProject = workspaces->containers().at(0);
    auto *restoredProject = workspaces->containers().at(1);
    QCOMPARE(manager->activeContainer(), restoredProject);
    QCOMPARE(firstProject->count(), 0);
    QVERIFY(!workspaces->projectIsLoaded(firstProject));
    QCOMPARE(restoredProject->count(), 1);

    auto *splitter = restoredProject->activeViewSplitter();
    QVERIFY(splitter != nullptr);
    const auto restoredTerminals = splitter->findChildren<TerminalDisplay *>();
    QCOMPARE(restoredTerminals.count(), emptyProject ? 1 : 3);
    if (emptyProject) {
        manager->splitLeftRight();
    }
    for (auto *terminal : restoredTerminals) {
        Q_EMIT terminal->requestToggleExpansion();
        QVERIFY(splitter->terminalMaximized());
        Q_EMIT terminal->requestToggleExpansion();
        QVERIFY(!splitter->terminalMaximized());
    }

    auto *movedTerminal = restoredTerminals.constLast();
    Q_EMIT movedTerminal->requestMoveToNewTab(movedTerminal);
    QCOMPARE(restoredProject->count(), 2);
    QCOMPARE(manager->containerForTerminal(movedTerminal), restoredProject);
    QCOMPARE(manager->activeContainer(), restoredProject);
    QCOMPARE(firstProject->count(), 0);
    QVERIFY(!workspaces->projectIsLoaded(firstProject));
}

void ViewManagerTest::testLastLoadedSessionExitsWithDeferredProject_data()
{
    QTest::addColumn<int>("activeProject");
    QTest::newRow("first-project-active") << 0;
    QTest::newRow("second-project-active") << 1;
}

void ViewManagerTest::testLastLoadedSessionExitsWithDeferredProject()
{
    QFETCH(int, activeProject);

    KConfig config(m_testDir->filePath(QStringLiteral("last-loaded-session-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    QJsonArray projects;
    for (int i = 0; i < 2; ++i) {
        const QJsonObject terminal{{QStringLiteral("SessionRestoreId"), i + 1},
                                   {QStringLiteral("Command"), QStringLiteral("/bin/sh")},
                                   {QStringLiteral("Arguments"), QJsonArray{QStringLiteral("/bin/sh")}},
                                   {QStringLiteral("WorkingDirectory"), m_testDir->path()}};
        const QJsonObject tab{{QStringLiteral("Orientation"), QStringLiteral("Horizontal")}, {QStringLiteral("Widgets"), QJsonArray{terminal}}};
        projects.append(QJsonObject{{QStringLiteral("Title"), QStringLiteral("Project %1").arg(i + 1)}, {QStringLiteral("Tabs"), QJsonArray{tab}}});
    }
    group.writeEntry("Projects", QJsonDocument(projects).toJson(QJsonDocument::Compact));
    group.writeEntry("ActiveProject", activeProject);

    auto window = MainWindow();
    auto *manager = window.viewManager();
    disconnect(manager, &ViewManager::empty, &window, &QWidget::close);
    QSignalSpy emptySpy(manager, &ViewManager::empty);
    auto *workspaces = manager->_workspaceContainer.data();
    manager->restoreSessions(group, false);

    QCOMPARE(workspaces->projectCount(), 2);
    QCOMPARE(manager->sessions().count(), 1);
    QPointer<TabbedViewContainer> closingProject = workspaces->containers().at(activeProject);
    QCOMPARE(manager->activeContainer(), closingProject.data());
    auto *remainingProject = workspaces->containers().at(1 - activeProject);
    QCOMPARE(remainingProject->count(), 0);
    QVERIFY(!workspaces->projectIsLoaded(remainingProject));

    auto *session = manager->sessions().constFirst();
    QVERIFY(session->isRunning());
    QSignalSpy finishedSpy(session, &Session::finished);
    session->sendText(QStringLiteral("exit\n"));
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(emptySpy.count(), 0);
    QTRY_VERIFY(closingProject.isNull());
    QCOMPARE(workspaces->projectCount(), 1);
    QCOMPARE(manager->activeContainer(), remainingProject);
    QVERIFY(workspaces->projectIsLoaded(remainingProject));
    QCOMPARE(remainingProject->count(), 1);
    QCOMPARE(manager->sessions().count(), 1);

    auto *remainingSession = manager->sessions().constFirst();
    QVERIFY(remainingSession->isRunning());
    remainingSession->sendText(QStringLiteral("exit\n"));
    QTRY_COMPARE(emptySpy.count(), 1);
}

void ViewManagerTest::testSaveSessionsPreservesDeferredProjectWorkspaces()
{
    KConfig sourceConfig(m_testDir->filePath(QStringLiteral("deferred-workspaces-source-testrc")), KConfig::SimpleConfig);
    KConfigGroup sourceGroup(&sourceConfig, QStringLiteral("Window"));

    {
        auto sourceWindow = MainWindow();
        auto *sourceManager = sourceWindow.viewManager();

        sourceWindow.newTab();
        auto *firstProject = sourceManager->activeContainer();
        sourceWindow.newTab();
        firstProject->setCurrentIndex(1);

        sourceManager->createProject();
        sourceWindow.newTab();
        sourceManager->saveSessions(sourceGroup);
    }

    const QJsonArray originalProjects = QJsonDocument::fromJson(sourceGroup.readEntry("Projects", QByteArray("[]"))).array();
    QCOMPARE(originalProjects.count(), 2);

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    auto *restoredWorkspaces = restoredManager->_workspaceContainer.data();
    restoredManager->restoreSessions(sourceGroup, false);

    const auto restoredProjects = restoredWorkspaces->containers();
    QCOMPARE(restoredProjects.at(0)->count(), 0);
    QCOMPARE(restoredWorkspaces->projectTabCount(restoredProjects.at(0)), 2);

    KConfig savedConfig(m_testDir->filePath(QStringLiteral("deferred-workspaces-saved-testrc")), KConfig::SimpleConfig);
    KConfigGroup savedGroup(&savedConfig, QStringLiteral("Window"));
    restoredManager->saveSessions(savedGroup);
    QCOMPARE(restoredProjects.at(0)->count(), 0);

    const QJsonArray savedProjects = QJsonDocument::fromJson(savedGroup.readEntry("Projects", QByteArray("[]"))).array();
    QCOMPARE(savedProjects.count(), 2);
    QCOMPARE(savedProjects.at(0).toObject()[QStringLiteral("Tabs")], originalProjects.at(0).toObject()[QStringLiteral("Tabs")]);
    QCOMPARE(savedProjects.at(0).toObject()[QStringLiteral("Active")], originalProjects.at(0).toObject()[QStringLiteral("Active")]);
    QCOMPARE(savedProjects.at(0).toObject()[QStringLiteral("LastDirectory")], originalProjects.at(0).toObject()[QStringLiteral("LastDirectory")]);
}

void ViewManagerTest::testRestoredProjectTitlesDoNotDuplicateDefaultTitle()
{
    KConfig config(m_testDir->filePath(QStringLiteral("restored-project-title-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    const QJsonObject restoredProject{{QStringLiteral("Title"), QStringLiteral("Project 2")}, {QStringLiteral("Tabs"), QJsonArray{}}};
    group.writeEntry("Projects", QJsonDocument(QJsonArray{restoredProject}).toJson(QJsonDocument::Compact));

    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *workspaces = manager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);

    manager->restoreSessions(group, false);
    QCOMPARE(workspaces->projectTitle(workspaces->containers().constFirst()), QStringLiteral("Project 2"));

    manager->createProject();
    QCOMPARE(workspaces->projectCount(), 2);
    QCOMPARE(workspaces->projectTitle(workspaces->containers().constLast()), QStringLiteral("Project 3"));
}

void ViewManagerTest::testColdRestorePreservesSessionProfileAndState()
{
    KConfig config(m_testDir->filePath(QStringLiteral("cold-restore-state-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    const QString profileName = QStringLiteral("Cold restore test profile");
    const QString profilePath = m_testDir->filePath(QStringLiteral("cold-restore.profile"));
    const QString program = QStringLiteral("/bin/sh");
    const QStringList arguments = {program, QStringLiteral("-c"), QStringLiteral("printf restored")};
    const QStringList environment = {QStringLiteral("TERM=xterm-256color"), QStringLiteral("KMUX_RESTORE_TEST=preserved")};
    const QString localTabTitle = QStringLiteral("restored local title");
    const QString remoteTabTitle = QStringLiteral("restored remote title");
    const QColor tabColor(QStringLiteral("#ff336699"));
    const QColor tabActivityColor(QStringLiteral("#ffcc8844"));

    {
        auto sourceWindow = MainWindow();
        Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
        profile->setHidden(true);
        profile->setProperty(Profile::Name, profileName);
        profile->setProperty(Profile::Path, profilePath);
        profile->setProperty(Profile::Command, program);
        profile->setProperty(Profile::Arguments, arguments);
        profile->setProperty(Profile::Environment, environment);

        Session *session = sourceWindow.createSession(profile, m_testDir->path());
        QVERIFY(session != nullptr);
        session->setAutoClose(false);
        session->setTabTitleFormat(Session::LocalTabTitle, localTabTitle);
        session->setTabTitleFormat(Session::RemoteTabTitle, remoteTabTitle);
        session->tabTitleSetByUser(true);
        session->setColor(tabColor);
        session->tabColorSetByUser(true);
        session->setActivityColor(tabActivityColor);
        session->tabActivityColorSetByUser(true);
        session->setBadgeEnabled(true);
        session->setBadgeText(QStringLiteral("restore badge"));
        sourceWindow.viewManager()->saveSessions(group);

        const auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
        const auto terminal =
            projects.at(0).toObject()[QStringLiteral("Tabs")].toArray().at(0).toObject()[QStringLiteral("Widgets")].toArray().at(0).toObject();
        QCOMPARE(terminal[QStringLiteral("ProfilePath")].toString(), profilePath);
        QCOMPARE(terminal[QStringLiteral("ProfileName")].toString(), profileName);
        QCOMPARE(terminal[QStringLiteral("Command")].toString(), program);
        QCOMPARE(terminal[QStringLiteral("Arguments")].toArray(), QJsonArray::fromStringList(arguments));
        QCOMPARE(terminal[QStringLiteral("Environment")].toArray(), QJsonArray::fromStringList(environment));
        QVERIFY(terminal[QStringLiteral("TabTitleSetByUser")].toBool());
        QVERIFY(terminal[QStringLiteral("TabColorSetByUser")].toBool());
        QVERIFY(terminal[QStringLiteral("TabActivityColorSetByUser")].toBool());
    }

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    restoredManager->restoreSessions(group, false);

    Session *restoredSession = restoredManager->activeViewController()->session();
    QVERIFY(restoredSession != nullptr);
    const Profile::Ptr restoredProfile = SessionManager::instance()->sessionProfile(restoredSession);
    QVERIFY(restoredProfile != nullptr);
    QCOMPARE(restoredProfile->name(), profileName);
    QCOMPARE(restoredProfile->path(), profilePath);
    QCOMPARE(restoredSession->program(), program);
    QCOMPARE(restoredSession->arguments(), arguments);
    QCOMPARE(restoredProfile->environment(), environment);
    QVERIFY(!restoredSession->autoClose());
    QCOMPARE(restoredSession->tabTitleFormat(Session::LocalTabTitle), localTabTitle);
    QCOMPARE(restoredSession->tabTitleFormat(Session::RemoteTabTitle), remoteTabTitle);
    QCOMPARE(restoredSession->color(), tabColor);
    QCOMPARE(restoredSession->activityColor(), tabActivityColor);
    QVERIFY(restoredSession->isTabTitleSetByUser());
    QVERIFY(restoredSession->isTabColorSetByUser());
    QVERIFY(restoredSession->isTabActivityColorSetByUser());
    QVERIFY(restoredSession->badgeEnabled());
    QCOMPARE(restoredSession->badgeText(), QStringLiteral("restore badge"));

    Profile::Ptr updatedProfile(new Profile(restoredProfile));
    updatedProfile->setProperty(Profile::LocalTabTitleFormat, QStringLiteral("profile local title"));
    updatedProfile->setProperty(Profile::RemoteTabTitleFormat, QStringLiteral("profile remote title"));
    updatedProfile->setProperty(Profile::TabColor, QColor(QStringLiteral("#ff112233")));
    updatedProfile->setProperty(Profile::TabActivityColor, QColor(QStringLiteral("#ff445566")));
    SessionManager::instance()->setSessionProfile(restoredSession, updatedProfile);

    QCOMPARE(restoredSession->tabTitleFormat(Session::LocalTabTitle), localTabTitle);
    QCOMPARE(restoredSession->tabTitleFormat(Session::RemoteTabTitle), remoteTabTitle);
    QCOMPARE(restoredSession->color(), tabColor);
    QCOMPARE(restoredSession->activityColor(), tabActivityColor);
}

void ViewManagerTest::testColdRestoreIgnoresEmptyEncoding()
{
    KConfig config(m_testDir->filePath(QStringLiteral("cold-restore-empty-encoding-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));
    const QByteArray profileEncoding("ISO-8859-1");

    {
        auto sourceWindow = MainWindow();
        Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
        profile->setHidden(true);
        profile->setProperty(Profile::Name, QStringLiteral("Empty encoding restore test profile"));
        profile->setProperty(Profile::DefaultEncoding, QString::fromLatin1(profileEncoding));

        Session *session = sourceWindow.createSession(profile, m_testDir->path());
        QVERIFY(session != nullptr);
        QCOMPARE(session->codec(), profileEncoding);
        sourceWindow.viewManager()->saveSessions(group);
    }

    auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    auto project = projects.at(0).toObject();
    auto tabs = project[QStringLiteral("Tabs")].toArray();
    auto tab = tabs.at(0).toObject();
    auto widgets = tab[QStringLiteral("Widgets")].toArray();
    auto terminal = widgets.at(0).toObject();
    terminal.insert(QStringLiteral("Encoding"), QString());
    widgets[0] = terminal;
    tab[QStringLiteral("Widgets")] = widgets;
    tabs[0] = tab;
    project[QStringLiteral("Tabs")] = tabs;
    projects[0] = project;
    group.writeEntry("Projects", QJsonDocument(projects).toJson(QJsonDocument::Compact));

    auto restoredWindow = MainWindow();
    auto *restoredManager = restoredWindow.viewManager();
    restoredManager->restoreSessions(group, false);

    Session *restoredSession = restoredManager->activeViewController()->session();
    QVERIFY(restoredSession != nullptr);
    QCOMPARE(restoredSession->codec(), profileEncoding);
}

void ViewManagerTest::testFinishedAutoCloseCommandIsNotColdRestored()
{
    const QString counterPath = m_testDir->filePath(QStringLiteral("finished-command-counter"));
    QFile::remove(counterPath);

    KConfig config(m_testDir->filePath(QStringLiteral("finished-command-state-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    auto sourceWindow = MainWindow();
    auto *sourceManager = sourceWindow.viewManager();
    disconnect(sourceManager, &ViewManager::empty, &sourceWindow, &QWidget::close);

    Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
    profile->setHidden(true);
    profile->setProperty(Profile::Command, QStringLiteral("/bin/sh"));
    profile->setProperty(Profile::Arguments,
                         QStringList{QStringLiteral("/bin/sh"), QStringLiteral("-c"), QStringLiteral("printf 'run\\n' >> %1").arg(counterPath)});

    Session *session = sourceWindow.createSession(profile, m_testDir->path());
    QVERIFY(session != nullptr);
    QVERIFY(session->autoClose());

    QSignalSpy emptySpy(sourceManager, &ViewManager::empty);
    bool processHadExitedWhenSaved = false;
    connect(sourceManager, &ViewManager::empty, this, [&]() {
        processHadExitedWhenSaved = session->hasProcessExited();
        sourceManager->saveSessions(group);
    });
    session->run();
    QTRY_COMPARE(emptySpy.count(), 1);
    QVERIFY(processHadExitedWhenSaved);

    QFile counter(counterPath);
    QVERIFY(counter.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(counter.readAll(), QByteArray("run\n"));
    counter.close();

    const auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    QCOMPARE(projects.count(), 1);
    QVERIFY(projects.at(0).toObject()[QStringLiteral("Tabs")].toArray().isEmpty());

    auto restoredWindow = MainWindow();
    restoredWindow.viewManager()->restoreSessions(group, false);
    QTest::qWait(100);

    QVERIFY(counter.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(counter.readAll(), QByteArray("run\n"));
}

void ViewManagerTest::testFinishedHeldCommandIsNotColdRestored()
{
    const QString counterPath = m_testDir->filePath(QStringLiteral("held-command-counter"));
    QFile::remove(counterPath);

    KConfig config(m_testDir->filePath(QStringLiteral("held-command-state-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    {
        auto sourceWindow = MainWindow();

        Profile::Ptr profile(new Profile(ProfileManager::instance()->defaultProfile()));
        profile->setHidden(true);
        profile->setProperty(Profile::Command, QStringLiteral("/bin/sh"));
        profile->setProperty(Profile::Arguments,
                             QStringList{QStringLiteral("/bin/sh"), QStringLiteral("-c"), QStringLiteral("printf 'run\\n' >> %1").arg(counterPath)});

        Session *session = sourceWindow.createSession(profile, m_testDir->path());
        QVERIFY(session != nullptr);
        // Like --hold: the tab stays open after the command finishes.
        session->setAutoClose(false);
        session->run();
        QTRY_VERIFY(session->hasProcessExited());

        sourceWindow.viewManager()->saveSessions(group);
    }

    QFile counter(counterPath);
    QVERIFY(counter.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(counter.readAll(), QByteArray("run\n"));
    counter.close();

    const auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    QCOMPARE(projects.count(), 1);
    QVERIFY(projects.at(0).toObject()[QStringLiteral("Tabs")].toArray().isEmpty());

    auto restoredWindow = MainWindow();
    restoredWindow.viewManager()->restoreSessions(group, false);
    QTest::qWait(100);

    QVERIFY(counter.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(counter.readAll(), QByteArray("run\n"));
}

void ViewManagerTest::testColdRestoreRecoversIncompleteTerminalState()
{
    KConfig config(m_testDir->filePath(QStringLiteral("incomplete-state-testrc")), KConfig::SimpleConfig);
    KConfigGroup group(&config, QStringLiteral("Window"));

    const QString directory = m_testDir->path();
    const QJsonObject terminalWithoutRestoreId{{QStringLiteral("WorkingDirectory"), directory}};
    const QJsonObject tabWithoutRestoreId{{QStringLiteral("Orientation"), QStringLiteral("Horizontal")},
                                          {QStringLiteral("Widgets"), QJsonArray{terminalWithoutRestoreId}}};
    const QJsonObject tabAfterActiveTab{{QStringLiteral("Orientation"), QStringLiteral("Horizontal")},
                                        {QStringLiteral("Widgets"), QJsonArray{QJsonObject{{QStringLiteral("SessionRestoreId"), 0}}}}};
    const QJsonObject emptySplitter{{QStringLiteral("Orientation"), QStringLiteral("Vertical")}, {QStringLiteral("Widgets"), QJsonArray{}}};
    const QJsonObject tabWithoutTerminals{{QStringLiteral("Orientation"), QStringLiteral("Horizontal")},
                                          {QStringLiteral("Widgets"), QJsonArray{emptySplitter}}};
    const QJsonArray projects{
        QJsonObject{{QStringLiteral("Title"), QStringLiteral("Incomplete")},
                    {QStringLiteral("Tabs"), QJsonArray{tabWithoutTerminals, tabWithoutRestoreId, tabAfterActiveTab}},
                    {QStringLiteral("Active"), 1}},
        QJsonObject{{QStringLiteral("Title"), QStringLiteral("Unusable")},
                    {QStringLiteral("Tabs"), QJsonArray{tabWithoutTerminals, QStringLiteral("not a tab")}}},
    };
    group.writeEntry("Projects", QJsonDocument(projects).toJson(QJsonDocument::Compact));
    group.writeEntry("ActiveProject", 0);

    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *workspaces = manager->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);
    manager->restoreSessions(group, false);

    const auto containers = workspaces->containers();
    QCOMPARE(containers.count(), 2);

    // Tabs without terminals are dropped without shifting the active tab; a
    // terminal without a restore ID still starts a session in its saved
    // directory.
    QCOMPARE(containers.at(0)->count(), 2);
    QCOMPARE(containers.at(0)->currentIndex(), 0);
    TerminalDisplay *restoredTerminal = containers.at(0)->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(restoredTerminal != nullptr);
    QCOMPARE(restoredTerminal->session()->initialWorkingDirectory(), directory);
    QTRY_VERIFY(restoredTerminal->session()->isRunning());

    // A project without usable tabs falls back to a default session.
    workspaces->activateProject(containers.at(1));
    QCOMPARE(containers.at(1)->count(), 1);
    TerminalDisplay *fallbackTerminal = containers.at(1)->activeViewSplitter()->activeTerminalDisplay();
    QVERIFY(fallbackTerminal != nullptr);
    QTRY_VERIFY(fallbackTerminal->session()->isRunning());
}

void ViewManagerTest::testCloseConfirmationSavesWorkspaceFirst()
{
    KConfigGroup group(KSharedConfig::openStateConfig(), QStringLiteral("LastProjectWorkspaceState"));
    group.deleteGroup();
    group.sync();

    auto window = MainWindow();
    window.newTab();
    window.newTab();
    auto *workspaces = window.viewManager()->_workspaceContainer.data();
    QVERIFY(workspaces != nullptr);
    const QString title = QStringLiteral("Saved before confirmation");
    workspaces->setProjectTitle(window.viewManager()->activeContainer(), title);

    // Two terminals make close() ask for confirmation. Cancel it, as a logout
    // that is forced while the question is open never answers it.
    bool confirmationShown = false;
    QTimer cancelTimer;
    connect(&cancelTimer, &QTimer::timeout, this, [&confirmationShown]() {
        const auto widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            auto *dialog = qobject_cast<QDialog *>(widget);
            if (dialog != nullptr && dialog->isVisible()) {
                confirmationShown = true;
                dialog->reject();
            }
        }
    });
    cancelTimer.start(50);
    QVERIFY(!window.close());
    cancelTimer.stop();
    QVERIFY(confirmationShown);

    group.config()->reparseConfiguration();
    const auto projects = QJsonDocument::fromJson(group.readEntry("Projects", QByteArray("[]"))).array();
    QCOMPARE(projects.count(), 1);
    QCOMPARE(projects.at(0).toObject()[QStringLiteral("Title")].toString(), title);
}

void ViewManagerTest::testInitializeRestoredSessionsPreservesActiveTabs()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *projects = manager->_workspaceContainer.data();
    QVERIFY(projects != nullptr);

    window.newTab();
    auto *firstProject = manager->activeContainer();
    window.newTab();
    firstProject->setCurrentIndex(0);

    manager->createProject();
    auto *secondProject = manager->activeContainer();
    window.newTab();
    secondProject->setCurrentIndex(1);
    projects->activateProject(firstProject);

    manager->initializeRestoredSessions();

    QCOMPARE(firstProject->currentIndex(), 0);
    QCOMPARE(secondProject->currentIndex(), 1);
    QCOMPARE(manager->activeContainer(), firstProject);
}

void ViewManagerTest::testRemovingBackgroundProjectPreservesActiveProject()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *projects = manager->_workspaceContainer.data();
    QVERIFY(projects != nullptr);

    auto *firstProject = manager->activeContainer();
    manager->createProject();
    auto *secondProject = manager->activeContainer();
    manager->createProject();
    auto *thirdProject = manager->activeContainer();
    QVERIFY(firstProject != secondProject);
    QVERIFY(secondProject != thirdProject);

    projects->activateProject(thirdProject);
    projects->removeProject(firstProject);

    QCOMPARE(projects->projectCount(), 2);
    QCOMPARE(manager->activeContainer(), thirdProject);

    projects->removeProject(thirdProject);

    QCOMPARE(projects->projectCount(), 1);
    QCOMPARE(manager->activeContainer(), secondProject);
}

void ViewManagerTest::testClosedProjectsDeleteViewContainers()
{
    auto window = MainWindow();
    auto *manager = window.viewManager();
    auto *projects = manager->_workspaceContainer.data();
    QVERIFY(projects != nullptr);

    QPointer<TabbedViewContainer> firstClosedContainer = manager->createContainer();
    projects->addProject(firstClosedContainer, QStringLiteral("First temporary project"));
    QPointer<TabbedViewContainer> secondClosedContainer = manager->createContainer();
    projects->addProject(secondClosedContainer, QStringLiteral("Second temporary project"));
    QCOMPARE(projects->projectCount(), 3);
    QCOMPARE(projects->findChildren<TabbedViewContainer *>().count(), 3);

    manager->containerEmptied(firstClosedContainer);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(firstClosedContainer.isNull());
    QCOMPARE(projects->projectCount(), 2);
    QCOMPARE(projects->findChildren<TabbedViewContainer *>().count(), 2);

    manager->containerEmptied(secondClosedContainer);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(secondClosedContainer.isNull());
    QCOMPARE(projects->projectCount(), 1);
    QCOMPARE(projects->findChildren<TabbedViewContainer *>().count(), 1);
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
