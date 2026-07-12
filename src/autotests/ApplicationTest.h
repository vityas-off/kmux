/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef APPLICATIONTEST_H
#define APPLICATIONTEST_H

#include <QObject>

namespace Konsole
{
class ApplicationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testInformationalArgumentsHandledLocally_data();
    void testInformationalArgumentsHandledLocally();
    void testActivationUsesRequestWorkingDirectory();
    void testProfileDirectoryPrecedence();
    void testActivationResolvesRelativeTabsFile();
    void testActivationResolvesRelativeLayoutFile();
    void testExplicitSessionRequestPreservesInitialWorkspace_data();
    void testExplicitSessionRequestPreservesInitialWorkspace();
    void testProfilePropertyCreatesTabOnActivation();
};
}

#endif
