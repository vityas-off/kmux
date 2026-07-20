/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../workspaces/ProjectWorkspaceModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace Konsole;

class ProjectWorkspaceModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testProjectStateAndOrdering();
    void testTitleValidation();
};

void ProjectWorkspaceModelTest::testProjectStateAndOrdering()
{
    ProjectWorkspaceModel model;
    QSignalSpy changedSpy(&model, &ProjectWorkspaceModel::projectChanged);

    const auto firstProject = model.addProject(QStringLiteral("First"));
    const auto secondProject = model.addProject(QStringLiteral("Second"));
    QVERIFY(!firstProject.isNull());
    QVERIFY(!secondProject.isNull());
    QVERIFY(firstProject != secondProject);
    QCOMPARE(model.projectIds(), QList<ProjectWorkspaceModel::ProjectId>({firstProject, secondProject}));
    QCOMPARE(model.nextProjectNumber(), 3);

    QVERIFY(model.setProjectTitle(firstProject, QStringLiteral("  Renamed  ")));
    QVERIFY(model.setProjectNotification(firstProject, QStringLiteral("  Build   finished  ")));
    QVERIFY(model.setProjectSummary(firstProject, QStringLiteral("active tab"), 4, 2, true, ProjectWorkspaceModel::ProjectStatus::Running));
    QCOMPARE(changedSpy.count(), 3);

    const auto project = model.project(firstProject);
    QCOMPARE(project.title, QStringLiteral("Renamed"));
    QCOMPARE(project.notification, QStringLiteral("Build finished"));
    QCOMPARE(project.subtitle, QStringLiteral("active tab"));
    QCOMPARE(project.tabCount, 4);
    QCOMPARE(project.activeProcessCount, 2);
    QVERIFY(project.hasActivity);
    QCOMPARE(project.status, ProjectWorkspaceModel::ProjectStatus::Running);
    QVERIFY(model.hasRunningProject());
    QVERIFY(!model.hasProjectNeedingInput());

    QVERIFY(model.setProjectSummary(firstProject, QStringLiteral("waiting"), 4, 2, true, ProjectWorkspaceModel::ProjectStatus::NeedsInput));
    QVERIFY(model.hasProjectNeedingInput());

    QVERIFY(model.reorderProjects({secondProject, firstProject}));
    QCOMPARE(model.projectIds(), QList<ProjectWorkspaceModel::ProjectId>({secondProject, firstProject}));
    QVERIFY(model.removeProject(secondProject));
    QCOMPARE(model.projectCount(), 1);
    QCOMPARE(model.projectAt(0).id, firstProject);
}

void ProjectWorkspaceModelTest::testTitleValidation()
{
    ProjectWorkspaceModel model;
    QSignalSpy addedSpy(&model, &ProjectWorkspaceModel::projectAdded);

    const auto invalidProject = model.addProject(QStringLiteral("  \t  "));
    QVERIFY(invalidProject.isNull());
    QCOMPARE(model.projectCount(), 0);
    QCOMPARE(model.nextProjectNumber(), 1);
    QCOMPARE(addedSpy.count(), 0);

    const auto projectId = model.addProject(QStringLiteral("  Valid title  "));
    QVERIFY(!projectId.isNull());
    QCOMPARE(model.project(projectId).title, QStringLiteral("Valid title"));
    QCOMPARE(addedSpy.count(), 1);

    QSignalSpy changedSpy(&model, &ProjectWorkspaceModel::projectChanged);
    QVERIFY(!model.setProjectTitle(projectId, QStringLiteral("   ")));
    QCOMPARE(model.project(projectId).title, QStringLiteral("Valid title"));
    QCOMPARE(changedSpy.count(), 0);
}

QTEST_GUILESS_MAIN(ProjectWorkspaceModelTest)

#include "ProjectWorkspaceModelTest.moc"
