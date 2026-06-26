/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTWORKSPACECONTAINER_H
#define PROJECTWORKSPACECONTAINER_H

#include <QList>
#include <QWidget>

#include "konsoleprivate_export.h"

class QListWidget;
class QListWidgetItem;
class QPoint;
class QStackedWidget;

namespace Konsole
{
class TabbedViewContainer;

class KONSOLEPRIVATE_EXPORT ProjectWorkspaceContainer : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectWorkspaceContainer(QWidget *parent = nullptr);
    ~ProjectWorkspaceContainer() override = default;

    int addProject(TabbedViewContainer *container, const QString &title);
    void removeProject(TabbedViewContainer *container);
    void activateProject(TabbedViewContainer *container);

    TabbedViewContainer *activeContainer() const;
    QList<TabbedViewContainer *> containers() const;
    TabbedViewContainer *containerForWidget(QWidget *widget) const;

    int projectCount() const;
    QString nextDefaultProjectTitle() const;
    void setProjectNavigationVisible(bool visible);

Q_SIGNALS:
    void newProjectRequested();
    void closeProjectRequested(TabbedViewContainer *container);
    void currentProjectChanged(TabbedViewContainer *container);

private Q_SLOTS:
    void currentRowChanged(int row);
    void openProjectContextMenu(const QPoint &point);
    void renameCurrentProject();

private:
    struct Project {
        QString title;
        TabbedViewContainer *container = nullptr;
    };

    int indexOf(TabbedViewContainer *container) const;
    void updateListItem(int index);
    void applyRailStyle();

    QList<Project> _projects;
    QWidget *_rail;
    QListWidget *_projectList;
    QStackedWidget *_stack;
    int _nextProjectNumber = 1;
};

}

#endif
