/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTWORKSPACECONTAINER_H
#define PROJECTWORKSPACECONTAINER_H

#include <QHash>
#include <QIcon>
#include <QList>
#include <QWidget>

#include "konsoleprivate_export.h"
#include "workspaces/ProjectWorkspaceModel.h"

class QListWidget;
class QListWidgetItem;
class QPoint;
class QSplitter;
class QStackedWidget;
class QTimer;

namespace Konsole
{
class TabbedViewContainer;

class KONSOLEPRIVATE_EXPORT ProjectWorkspaceContainer : public QWidget
{
    Q_OBJECT

public:
    using ProjectStatus = ProjectWorkspaceModel::ProjectStatus;

    explicit ProjectWorkspaceContainer(QWidget *parent = nullptr);
    ~ProjectWorkspaceContainer() override = default;

    int addProject(TabbedViewContainer *container, const QString &title);
    void removeProject(TabbedViewContainer *container);
    void activateProject(TabbedViewContainer *container);

    TabbedViewContainer *activeContainer() const;
    QList<TabbedViewContainer *> containers() const;
    TabbedViewContainer *containerForWidget(QWidget *widget) const;
    QString projectTitle(TabbedViewContainer *container) const;
    void setProjectTitle(TabbedViewContainer *container, const QString &title);
    QString projectSubtitle(TabbedViewContainer *container) const;
    int projectTabCount(TabbedViewContainer *container) const;
    int projectActiveProcessCount(TabbedViewContainer *container) const;
    bool projectHasActivity(TabbedViewContainer *container) const;
    ProjectStatus projectStatus(TabbedViewContainer *container) const;
    QString projectNotification(TabbedViewContainer *container) const;
    void setProjectNotification(TabbedViewContainer *container, const QString &notification);
    void setProjectSummary(TabbedViewContainer *container,
                           const QString &subtitle,
                           int tabCount,
                           int activeProcessCount,
                           bool hasActivity,
                           ProjectStatus status = ProjectStatus::None,
                           const QIcon &icon = {});

    int projectCount() const;
    QString nextDefaultProjectTitle() const;
    ProjectWorkspaceModel *projectModel() const;
    void setProjectNavigationVisible(bool visible);
    int projectRailWidth() const;
    void setProjectRailWidth(int requestedWidth);

Q_SIGNALS:
    void newProjectRequested();
    void closeProjectRequested(TabbedViewContainer *container);
    void currentProjectChanged(TabbedViewContainer *container);

private Q_SLOTS:
    void currentRowChanged(int row);
    void openProjectContextMenu(const QPoint &point);
    void renameCurrentProject();
    void syncProjectsToListOrder();

private:
    int indexOf(TabbedViewContainer *container) const;
    ProjectWorkspaceModel::ProjectId projectId(TabbedViewContainer *container) const;
    TabbedViewContainer *containerAt(int index) const;
    void updateListItem(int index);
    void applyRailStyle();
    void updateStatusAnimationTimer();

    ProjectWorkspaceModel *_model;
    QHash<TabbedViewContainer *, ProjectWorkspaceModel::ProjectId> _projectIds;
    QHash<ProjectWorkspaceModel::ProjectId, TabbedViewContainer *> _containers;
    QWidget *_rail;
    QSplitter *_splitter;
    QListWidget *_projectList;
    QStackedWidget *_stack;
    QTimer *_statusAnimationTimer;
    int _projectRailWidth;
};

}

#endif
