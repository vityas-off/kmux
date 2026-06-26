/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widgets/ProjectWorkspaceContainer.h"

#include "widgets/ViewContainer.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <KLocalizedString>

using namespace Konsole;

ProjectWorkspaceContainer::ProjectWorkspaceContainer(QWidget *parent)
    : QWidget(parent)
    , _rail(new QWidget(this))
    , _projectList(new QListWidget(this))
    , _stack(new QStackedWidget(this))
    , _newProjectButton(new QToolButton(this))
{
    _rail->setObjectName(QStringLiteral("projectRail"));
    _rail->setFixedWidth(164);

    _newProjectButton->setAutoRaise(true);
    _newProjectButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    _newProjectButton->setToolTip(i18nc("@info:tooltip", "New Project"));
    connect(_newProjectButton, &QToolButton::clicked, this, &ProjectWorkspaceContainer::newProjectRequested);

    _projectList->setObjectName(QStringLiteral("projectList"));
    _projectList->setFrameShape(QFrame::NoFrame);
    _projectList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _projectList->setSelectionMode(QAbstractItemView::SingleSelection);
    _projectList->setContextMenuPolicy(Qt::CustomContextMenu);
    _projectList->setSpacing(2);
    connect(_projectList, &QListWidget::currentRowChanged, this, &ProjectWorkspaceContainer::currentRowChanged);
    connect(_projectList, &QListWidget::itemDoubleClicked, this, &ProjectWorkspaceContainer::renameCurrentProject);
    connect(_projectList, &QListWidget::customContextMenuRequested, this, &ProjectWorkspaceContainer::openProjectContextMenu);

    auto *railLayout = new QVBoxLayout(_rail);
    railLayout->setContentsMargins(6, 6, 6, 6);
    railLayout->setSpacing(6);
    railLayout->addWidget(_newProjectButton, 0, Qt::AlignLeft);
    railLayout->addWidget(_projectList, 1);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(_rail);
    rootLayout->addWidget(_stack, 1);

    applyRailStyle();
}

int ProjectWorkspaceContainer::addProject(TabbedViewContainer *container, const QString &title)
{
    Q_ASSERT(container != nullptr);

    Project project;
    project.title = title;
    project.container = container;
    _projects.append(project);

    const int index = _projects.count() - 1;
    _stack->addWidget(container);
    auto *item = new QListWidgetItem(_projectList);
    item->setSizeHint(QSize(1, 30));
    updateListItem(index);

    _nextProjectNumber = qMax(_nextProjectNumber, index + 2);
    _projectList->setCurrentRow(index);
    return index;
}

void ProjectWorkspaceContainer::removeProject(TabbedViewContainer *container)
{
    const int index = indexOf(container);
    if (index < 0) {
        return;
    }

    QWidget *widget = _stack->widget(index);
    _stack->removeWidget(widget);

    delete _projectList->takeItem(index);
    _projects.removeAt(index);

    if (_projects.isEmpty()) {
        return;
    }

    const int nextIndex = qMin(index, _projects.count() - 1);
    _projectList->setCurrentRow(nextIndex);
}

void ProjectWorkspaceContainer::activateProject(TabbedViewContainer *container)
{
    const int index = indexOf(container);
    if (index >= 0) {
        _projectList->setCurrentRow(index);
    }
}

TabbedViewContainer *ProjectWorkspaceContainer::activeContainer() const
{
    const int row = _projectList->currentRow();
    if (row < 0 || row >= _projects.count()) {
        return nullptr;
    }

    return _projects.at(row).container;
}

QList<TabbedViewContainer *> ProjectWorkspaceContainer::containers() const
{
    QList<TabbedViewContainer *> result;
    result.reserve(_projects.count());
    for (const Project &project : _projects) {
        result.append(project.container);
    }
    return result;
}

TabbedViewContainer *ProjectWorkspaceContainer::containerForWidget(QWidget *widget) const
{
    if (widget == nullptr) {
        return nullptr;
    }

    for (const Project &project : _projects) {
        if (project.container == widget || project.container->isAncestorOf(widget)) {
            return project.container;
        }
    }

    return nullptr;
}

int ProjectWorkspaceContainer::projectCount() const
{
    return _projects.count();
}

QString ProjectWorkspaceContainer::nextDefaultProjectTitle() const
{
    return i18nc("@title", "Project %1", _nextProjectNumber);
}

void ProjectWorkspaceContainer::setProjectNavigationVisible(bool visible)
{
    _rail->setVisible(visible);
}

void ProjectWorkspaceContainer::currentRowChanged(int row)
{
    if (row < 0 || row >= _projects.count()) {
        return;
    }

    _stack->setCurrentWidget(_projects.at(row).container);
    Q_EMIT currentProjectChanged(_projects.at(row).container);
}

void ProjectWorkspaceContainer::openProjectContextMenu(const QPoint &point)
{
    auto *item = _projectList->itemAt(point);
    if (item != nullptr) {
        _projectList->setCurrentItem(item);
    }

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:inmenu", "New Project"), this, [this] {
        Q_EMIT newProjectRequested();
    });

    auto *renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu", "Rename Project..."), this, [this] {
        renameCurrentProject();
    });
    renameAction->setEnabled(activeContainer() != nullptr);

    auto *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close Project"), this, [this] {
        if (auto *container = activeContainer()) {
            Q_EMIT closeProjectRequested(container);
        }
    });
    closeAction->setEnabled(projectCount() > 1 && activeContainer() != nullptr);

    menu.exec(_projectList->mapToGlobal(point));
}

void ProjectWorkspaceContainer::renameCurrentProject()
{
    const int row = _projectList->currentRow();
    if (row < 0 || row >= _projects.count()) {
        return;
    }

    bool ok = false;
    const QString title = QInputDialog::getText(this,
                                                i18nc("@title:window", "Rename Project"),
                                                i18nc("@label:textbox", "Project name:"),
                                                QLineEdit::Normal,
                                                _projects.at(row).title,
                                                &ok);
    if (!ok || title.trimmed().isEmpty()) {
        return;
    }

    _projects[row].title = title.trimmed();
    updateListItem(row);
}

int ProjectWorkspaceContainer::indexOf(TabbedViewContainer *container) const
{
    for (int i = 0; i < _projects.count(); ++i) {
        if (_projects.at(i).container == container) {
            return i;
        }
    }
    return -1;
}

void ProjectWorkspaceContainer::updateListItem(int index)
{
    if (index < 0 || index >= _projects.count()) {
        return;
    }

    auto *item = _projectList->item(index);
    if (item == nullptr) {
        return;
    }

    item->setText(_projects.at(index).title);
    item->setToolTip(_projects.at(index).title);
    item->setIcon(QIcon::fromTheme(QStringLiteral("folder")));
}

void ProjectWorkspaceContainer::applyRailStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QWidget#projectRail {
            background: palette(window);
            border-right: 1px solid palette(mid);
        }
        QListWidget#projectList {
            background: transparent;
            outline: 0;
        }
        QListWidget#projectList::item {
            padding: 5px 7px;
            border-radius: 4px;
        }
        QListWidget#projectList::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
}

#include "moc_ProjectWorkspaceContainer.cpp"
