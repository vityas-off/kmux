/*
    SPDX-FileCopyrightText: 2025 Akseli Lahtinen <akselmo@akselmo.dev>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIEWMANAGERTEST_H
#define VIEWMANAGERTEST_H

#include <QObject>
#include <QTemporaryDir>

namespace Konsole
{
class ViewManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testSaveLayout();
    void testLoadLayout();
    void testSavedSessionSettingsMarkWorkspaceChanged();
    void testProjectWorkspacesKeepIndependentTabs();
    void testTabHistoryShortcutsStayInActiveProject();
    void testFinishedBackgroundSessionIsRemovedFromTabHistory();
    void testSplitsStayInActiveProjectWorkspace();
    void testDbusLayoutOperationsRejectCrossProjectViews();
    void testSessionCountIncludesAllProjectWorkspaces();
    void testSessionsIncludesAllProjectWorkspaces();
    void testProjectWorkspaceSummaryTracksActiveTab();
    void testProjectWorkspaceTerminalNotificationMarksInactiveProject();
    void testProjectWorkspaceActivityClearsWhenTerminalRefocused();
    void testProjectWorkspaceStatusTracksSessionHooks();
    void testTerminalTabsTrackSessionStatusesIndependently();
    void testForegroundProcessAndIdleAgentUseDifferentStatuses();
    void testRunningAgentsControlSleepInhibition();
    void testProjectWorkspaceStatusClearsWhenAgentExits();
    void testProjectWorkspaceStatusClearsWhenAgentReturnsToShell();
    void testProjectWorkspaceAgentSessionDoesNotInheritAnotherAgentPid();
    void testProjectWorkspaceCodexDecisionKeysAreSessionScoped();
    void testProjectWorkspaceAgentInterruptClearsRunningStatus();
    void testProjectWorkspaceClaudeEscapeFollowsTitle();
    void testProjectWorkspaceClaudeEscapeKeepsPermissionPrompt();
    void testProjectWorkspaceCodexAutoReviewedPermissionStaysRunning();
    void testProjectWorkspaceClaudeDenialDoesNotOverrideStop();
    void testProjectWorkspaceClaudeIdlePromptMarksInactiveProject();
    void testProjectWorkspaceClaudeIdlePromptClearsNotification();
    void testProjectWorkspaceClaudeIdlePromptPreservesPermissionRequest();
    void testProjectWorkspaceClaudeIdlePromptKeepsBackgroundWorkRunning();
    void testProjectWorkspaceClaudeRateLimitPersistsUntilResumed();
    void testProjectWorkspaceClaudeCompactionStartsNewPrompt();
    void testProjectWorkspaceClaudeRejectsStaleHookIdentities();
    void testProjectWorkspaceClaudeTaskNotificationBeginsTurn();
    void testProjectWorkspaceClaudeSubagentResolutionClearsOnlyNotification();
    void testProjectWorkspaceClaudeDecisionClearsOnTerminalInput();
    void testProjectWorkspaceTracksMultipleCodexDecisionsInOneSession();
    void testSessionSignalsAreHandledOnceAcrossMultipleViews();
    void testProjectWorkspaceNavigationShortcuts();
    void testProjectWorkspaceRailDoesNotAcceptFocus();
    void testSelectedProjectFollowsRailBackground_data();
    void testSelectedProjectFollowsRailBackground();
    void testProjectRailFollowsPaletteChange();
    void testNoNavigationDisablesProjectActions();
    void testProjectWorkspaceDetachActionsDisabled();
    void testProjectWorkspaceNewWindowActionDisabled();
    void testUnsupportedHelpActionsHidden();
    void testMoveTabBetweenProjectWorkspaces();
    void testMoveTabMenuSurvivesChangesWhileOpen();
    void testSaveSessionsStoresProjectWorkspaces();
    void testProjectWorkspaceRailWidthPersists();
    void testProjectIconsPersistWithoutLoadingInactiveProjects();
    void testRestoreSessionsLazilyCreatesProjectWorkspacesWithoutSessionIds();
    void testRestoredTerminalActionsStayInProject_data();
    void testRestoredTerminalActionsStayInProject();
    void testLastLoadedSessionExitsWithDeferredProject_data();
    void testLastLoadedSessionExitsWithDeferredProject();
    void testSaveSessionsPreservesDeferredProjectWorkspaces();
    void testRestoredProjectTitlesDoNotDuplicateDefaultTitle();
    void testColdRestorePreservesSessionProfileAndState();
    void testColdRestoreAppliesLaunchSettingsToSharedProfile();
    void testColdRestoreUsesStoredProfileOfTemporaryProfile();
    void testColdRestorePreservesSplitSizesAndFocusedTerminal();
    void testColdRestoreIgnoresEmptyEncoding();
    void testColdRestorePreservesShellSessionId();
    void testColdRestoreDoesNotReuseLiveShellSessionId();
    void testFinishedAutoCloseCommandIsNotColdRestored();
    void testFinishedHeldCommandIsNotColdRestored();
    void testFinishedHeldTabDoesNotShiftActiveTab();
    void testColdRestoreRecoversIncompleteTerminalState();
    void testCloseConfirmationSavesWorkspaceFirst();
    void testInitializeRestoredSessionsPreservesActiveTabs();
    void testRemovingBackgroundProjectPreservesActiveProject();
    void testClosedProjectsDeleteViewContainers();
    void testContainerMenuLaunchKeepsPendingColor();

private:
    QTemporaryDir *m_testDir;
};

}

#endif // VIEWMANAGERTEST_H
