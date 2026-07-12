/*
    SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIEWMANAGERTEST_H
#define VIEWMANAGERTEST_H

#include <QTemporaryDir>
#include <kde_terminal_interface.h>

namespace Konsole
{
class ViewManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testSaveLayout();
    void testLoadLayout();
    void testProjectWorkspacesKeepIndependentTabs();
    void testTabHistoryShortcutsStayInActiveProject();
    void testFinishedBackgroundSessionIsRemovedFromTabHistory();
    void testSplitsStayInActiveProjectWorkspace();
    void testDbusLayoutOperationsRejectCrossProjectViews();
    void testSessionCountIncludesAllProjectWorkspaces();
    void testSessionsIncludesAllProjectWorkspaces();
    void testProjectWorkspaceSummaryTracksActiveTab();
    void testProjectWorkspaceTerminalNotificationMarksInactiveProject();
    void testProjectWorkspaceStatusTracksSessionHooks();
    void testProjectWorkspaceStatusClearsWhenAgentExits();
    void testProjectWorkspaceAgentSessionDoesNotInheritAnotherAgentPid();
    void testProjectWorkspaceCodexDecisionKeysAreSessionScoped();
    void testProjectWorkspaceTracksMultipleCodexDecisionsInOneSession();
    void testSessionSignalsAreHandledOnceAcrossMultipleViews();
    void testProjectWorkspaceNavigationShortcuts();
    void testProjectWorkspaceRailDoesNotAcceptFocus();
    void testNoNavigationDisablesProjectActions();
    void testProjectWorkspaceDetachActionsDisabled();
    void testProjectWorkspaceNewWindowActionDisabled();
    void testUnsupportedHelpActionsHidden();
    void testMoveTabBetweenProjectWorkspaces();
    void testSaveSessionsStoresProjectWorkspaces();
    void testProjectWorkspaceRailWidthPersists();
    void testRestoreSessionsCreatesProjectWorkspacesWithoutSessionIds();
    void testRestoredProjectTitlesDoNotDuplicateDefaultTitle();
    void testColdRestorePreservesSessionProfileAndState();
    void testFinishedAutoCloseCommandIsNotColdRestored();
    void testInitializeRestoredSessionsPreservesActiveTabs();
    void testRemovingBackgroundProjectPreservesActiveProject();
    void testClosedProjectsDeleteViewContainers();
    void testContainerMenuLaunchKeepsPendingColor();

private:
    QTemporaryDir *m_testDir;
};

}

#endif // VIEWMANAGERTEST_H
