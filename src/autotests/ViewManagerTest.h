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
    void testSplitsStayInActiveProjectWorkspace();
    void testSessionCountUsesActiveProjectWorkspace();
    void testSessionsIncludesAllProjectWorkspaces();
    void testProjectWorkspaceSummaryTracksActiveTab();
    void testProjectWorkspaceTerminalNotificationMarksInactiveProject();
    void testProjectWorkspaceStatusTracksSessionHooks();
    void testProjectWorkspaceNavigationShortcuts();
    void testNoNavigationDisablesProjectActions();
    void testProjectWorkspaceDetachActionsDisabled();
    void testProjectWorkspaceNewWindowActionDisabled();
    void testMoveTabBetweenProjectWorkspaces();
    void testSaveSessionsStoresProjectWorkspaces();
    void testProjectWorkspaceRailWidthPersists();
    void testRestoreSessionsCreatesProjectWorkspacesWithoutSessionIds();
    void testColdRestorePreservesSessionProfileAndState();
    void testInitializeRestoredSessionsPreservesActiveTabs();
    void testContainerMenuLaunchKeepsPendingColor();

private:
    QTemporaryDir *m_testDir;
};

}

#endif // VIEWMANAGERTEST_H
