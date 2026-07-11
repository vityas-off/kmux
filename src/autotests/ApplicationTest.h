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
    void testActivationUsesRequestWorkingDirectory();
    void testActivationResolvesRelativeTabsFile();
    void testActivationResolvesRelativeLayoutFile();
    void testProfilePropertySkipsInitialWorkspaceRestore();
    void testProfilePropertyCreatesTabOnActivation();
};
}

#endif
