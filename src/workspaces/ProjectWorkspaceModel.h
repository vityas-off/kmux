/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROJECTWORKSPACEMODEL_H
#define PROJECTWORKSPACEMODEL_H

#include "konsoleprivate_export.h"

#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace Konsole
{
class KONSOLEPRIVATE_EXPORT ProjectWorkspaceModel : public QObject
{
    Q_OBJECT

public:
    using ProjectId = QUuid;

    enum class ProjectStatus {
        None,
        Running,
        Idle,
        NeedsInput,
    };
    Q_ENUM(ProjectStatus)

    struct ProjectData {
        ProjectId id;
        QString title;
        QString subtitle;
        QIcon icon;
        int tabCount = 0;
        int activeProcessCount = 0;
        bool hasActivity = false;
        ProjectStatus status = ProjectStatus::None;
        QString notification;
    };

    explicit ProjectWorkspaceModel(QObject *parent = nullptr);

    ProjectId addProject(const QString &title);
    bool removeProject(const ProjectId &id);
    bool reorderProjects(const QList<ProjectId> &orderedIds);

    int projectCount() const;
    int indexOf(const ProjectId &id) const;
    ProjectData projectAt(int index) const;
    ProjectData project(const ProjectId &id) const;
    QList<ProjectId> projectIds() const;

    bool setProjectTitle(const ProjectId &id, const QString &title);
    bool setProjectNotification(const ProjectId &id, const QString &notification);
    bool setProjectSummary(const ProjectId &id,
                           const QString &subtitle,
                           int tabCount,
                           int activeProcessCount,
                           bool hasActivity,
                           ProjectStatus status,
                           const QIcon &icon = {});

    int nextProjectNumber() const;
    bool hasRunningProject() const;
    bool hasProjectNeedingInput() const;

Q_SIGNALS:
    void projectAdded(const Konsole::ProjectWorkspaceModel::ProjectId &id, int index);
    void projectRemoved(const Konsole::ProjectWorkspaceModel::ProjectId &id, int index);
    void projectChanged(const Konsole::ProjectWorkspaceModel::ProjectId &id);
    void projectsReordered();

private:
    QList<ProjectData> _projects;
    int _nextProjectNumber = 1;
};
}

#endif
