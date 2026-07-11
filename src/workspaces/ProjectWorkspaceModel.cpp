/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "workspaces/ProjectWorkspaceModel.h"

#include <QSet>

#include <algorithm>

using namespace Konsole;

ProjectWorkspaceModel::ProjectWorkspaceModel(QObject *parent)
    : QObject(parent)
{
}

ProjectWorkspaceModel::ProjectId ProjectWorkspaceModel::addProject(const QString &title)
{
    ProjectData project;
    project.id = QUuid::createUuid();
    project.title = title.trimmed();
    _projects.append(project);
    _nextProjectNumber = qMax(_nextProjectNumber, _projects.count() + 1);
    Q_EMIT projectAdded(project.id, _projects.count() - 1);
    return project.id;
}

bool ProjectWorkspaceModel::removeProject(const ProjectId &id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }

    _projects.removeAt(index);
    Q_EMIT projectRemoved(id, index);
    return true;
}

bool ProjectWorkspaceModel::reorderProjects(const QList<ProjectId> &orderedIds)
{
    if (orderedIds.count() != _projects.count() || QSet<ProjectId>(orderedIds.cbegin(), orderedIds.cend()).count() != _projects.count()) {
        return false;
    }
    for (const ProjectId &id : orderedIds) {
        if (indexOf(id) < 0) {
            return false;
        }
    }

    QList<ProjectData> reorderedProjects;
    reorderedProjects.reserve(_projects.count());
    for (const ProjectId &id : orderedIds) {
        reorderedProjects.append(project(id));
    }
    _projects = reorderedProjects;
    Q_EMIT projectsReordered();
    return true;
}

int ProjectWorkspaceModel::projectCount() const
{
    return _projects.count();
}

int ProjectWorkspaceModel::indexOf(const ProjectId &id) const
{
    const auto iterator = std::find_if(_projects.cbegin(), _projects.cend(), [&id](const ProjectData &project) {
        return project.id == id;
    });
    return iterator == _projects.cend() ? -1 : std::distance(_projects.cbegin(), iterator);
}

ProjectWorkspaceModel::ProjectData ProjectWorkspaceModel::projectAt(int index) const
{
    return index >= 0 && index < _projects.count() ? _projects.at(index) : ProjectData{};
}

ProjectWorkspaceModel::ProjectData ProjectWorkspaceModel::project(const ProjectId &id) const
{
    return projectAt(indexOf(id));
}

QList<ProjectWorkspaceModel::ProjectId> ProjectWorkspaceModel::projectIds() const
{
    QList<ProjectId> ids;
    ids.reserve(_projects.count());
    for (const ProjectData &project : _projects) {
        ids.append(project.id);
    }
    return ids;
}

bool ProjectWorkspaceModel::setProjectTitle(const ProjectId &id, const QString &title)
{
    const int index = indexOf(id);
    const QString normalizedTitle = title.trimmed();
    if (index < 0 || normalizedTitle.isEmpty()) {
        return false;
    }

    _projects[index].title = normalizedTitle;
    Q_EMIT projectChanged(id);
    return true;
}

bool ProjectWorkspaceModel::setProjectNotification(const ProjectId &id, const QString &notification)
{
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }

    _projects[index].notification = notification.simplified();
    Q_EMIT projectChanged(id);
    return true;
}

bool ProjectWorkspaceModel::setProjectSummary(const ProjectId &id,
                                              const QString &subtitle,
                                              int tabCount,
                                              int activeProcessCount,
                                              bool hasActivity,
                                              ProjectStatus status,
                                              const QIcon &icon)
{
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }

    auto &project = _projects[index];
    project.subtitle = subtitle;
    project.tabCount = qMax(0, tabCount);
    project.activeProcessCount = qMax(0, activeProcessCount);
    project.hasActivity = hasActivity;
    project.status = status;
    project.icon = icon;
    Q_EMIT projectChanged(id);
    return true;
}

int ProjectWorkspaceModel::nextProjectNumber() const
{
    return _nextProjectNumber;
}

bool ProjectWorkspaceModel::hasRunningProject() const
{
    return std::any_of(_projects.cbegin(), _projects.cend(), [](const ProjectData &project) {
        return project.status == ProjectStatus::Running;
    });
}

bool ProjectWorkspaceModel::hasProjectNeedingInput() const
{
    return std::any_of(_projects.cbegin(), _projects.cend(), [](const ProjectData &project) {
        return project.status == ProjectStatus::NeedsInput;
    });
}

#include "moc_ProjectWorkspaceModel.cpp"
