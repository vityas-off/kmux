/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widgets/ProjectWorkspaceContainer.h"

#include "widgets/ViewContainer.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <KLocalizedString>

#include <functional>

using namespace Konsole;

namespace
{
enum ProjectRoles {
    ContainerRole = Qt::UserRole,
    SubtitleRole,
    TabCountRole,
    ActiveProcessCountRole,
    HasActivityRole,
};

QString badgeText(int count)
{
    return count > 99 ? QStringLiteral("99+") : QString::number(count);
}

class ProjectItemDelegate : public QStyledItemDelegate
{
public:
    explicit ProjectItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QSize base = QStyledItemDelegate::sizeHint(option, index);
        return {base.width(), qMax(base.height(), 56)};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        const QIcon icon = itemOption.icon.isNull() ? QIcon::fromTheme(QStringLiteral("folder")) : itemOption.icon;
        itemOption.text.clear();
        itemOption.icon = {};

        auto *style = itemOption.widget != nullptr ? itemOption.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &itemOption, painter, itemOption.widget);

        const QRect rect = itemOption.rect.adjusted(8, 7, -12, -7);
        const bool selected = itemOption.state.testFlag(QStyle::State_Selected);
        const QPalette::ColorRole textRole = selected ? QPalette::HighlightedText : QPalette::Text;
        const QPalette::ColorRole subtleRole = selected ? QPalette::HighlightedText : QPalette::PlaceholderText;
        const QColor textColor = itemOption.palette.color(textRole);
        QColor subtleColor = itemOption.palette.color(subtleRole);
        if (selected) {
            subtleColor.setAlpha(245);
        }

        const QSize iconSize(18, 18);
        const QRect iconRect(rect.left(), rect.top() + (rect.height() - iconSize.height()) / 2, iconSize.width(), iconSize.height());
        icon.paint(painter, iconRect, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);

        const int tabCount = index.data(TabCountRole).toInt();
        const int processCount = index.data(ActiveProcessCountRole).toInt();
        const bool hasActivity = index.data(HasActivityRole).toBool();

        QRect contentRect = rect.adjusted(iconSize.width() + 8, 0, 0, 0);
        const QFontMetrics titleMetrics(itemOption.font);
        const QString tabsBadge = tabCount > 0 ? badgeText(tabCount) : QString();
        QRect tabsBadgeRect;
        if (!tabsBadge.isEmpty()) {
            const int badgeWidth = qMax(22, titleMetrics.horizontalAdvance(tabsBadge) + 12);
            tabsBadgeRect = QRect(rect.right() - badgeWidth, rect.top() + 2, badgeWidth, 18);
            contentRect.setRight(tabsBadgeRect.left() - 8);
        }

        QRect processBadgeRect;
        const QString processBadge = processCount > 0 ? badgeText(processCount) : QString();
        if (!processBadge.isEmpty()) {
            const int badgeWidth = qMax(22, titleMetrics.horizontalAdvance(processBadge) + 12);
            processBadgeRect = QRect(rect.right() - badgeWidth, rect.bottom() - 19, badgeWidth, 18);
            contentRect.setRight(qMin(contentRect.right(), processBadgeRect.left() - 8));
        } else if (hasActivity) {
            processBadgeRect = QRect(rect.right() - 13, rect.bottom() - 15, 10, 10);
            contentRect.setRight(qMin(contentRect.right(), processBadgeRect.left() - 8));
        }

        QFont titleFont = itemOption.font;
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(textColor);
        const QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, contentRect.width());
        painter->drawText(contentRect.left(), contentRect.top(), contentRect.width(), contentRect.height() / 2, Qt::AlignLeft | Qt::AlignVCenter, title);

        QFont subtitleFont = itemOption.font;
        if (!selected) {
            subtitleFont.setPointSize(qMax(1, subtitleFont.pointSize() - 1));
        }
        painter->setFont(subtitleFont);
        painter->setPen(subtleColor);
        const QFontMetrics subtitleMetrics(subtitleFont);
        const QString subtitle = subtitleMetrics.elidedText(index.data(SubtitleRole).toString(), Qt::ElideRight, contentRect.width());
        painter->drawText(contentRect.left(), rect.center().y(), contentRect.width(), rect.height() / 2, Qt::AlignLeft | Qt::AlignVCenter, subtitle);

        if (!tabsBadgeRect.isNull()) {
            QColor badgeBg = selected ? itemOption.palette.color(QPalette::HighlightedText) : itemOption.palette.color(QPalette::Mid);
            QColor badgeFg = selected ? itemOption.palette.color(QPalette::Highlight) : itemOption.palette.color(QPalette::Text);
            badgeBg.setAlpha(selected ? 230 : 90);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeBg);
            painter->drawRoundedRect(tabsBadgeRect.adjusted(0, 0, -1, -1), 8, 8);
            painter->setPen(badgeFg);
            painter->setFont(itemOption.font);
            painter->drawText(tabsBadgeRect, Qt::AlignCenter, tabsBadge);
        }

        if (!processBadgeRect.isNull()) {
            QColor indicator = itemOption.palette.color(QPalette::Highlight);
            if (processCount > 0) {
                indicator = QColor::fromRgb(47, 160, 96);
                painter->setPen(Qt::NoPen);
                painter->setBrush(indicator);
                painter->drawRoundedRect(processBadgeRect.adjusted(0, 0, -1, -1), 8, 8);
                painter->setPen(Qt::white);
                painter->setFont(itemOption.font);
                painter->drawText(processBadgeRect, Qt::AlignCenter, processBadge);
            } else {
                painter->setPen(Qt::NoPen);
                painter->setBrush(indicator);
                painter->drawEllipse(processBadgeRect);
            }
        }

        painter->restore();
    }
};

class ProjectListWidget : public QListWidget
{
public:
    explicit ProjectListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
    }

    std::function<void()> itemsDropped;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QListWidget::dropEvent(event);
        if (event->isAccepted() && itemsDropped) {
            itemsDropped();
        }
    }
};

}

ProjectWorkspaceContainer::ProjectWorkspaceContainer(QWidget *parent)
    : QWidget(parent)
    , _rail(new QWidget(this))
    , _splitter(new QSplitter(Qt::Horizontal, this))
    , _projectList(new ProjectListWidget(this))
    , _stack(new QStackedWidget(this))
{
    _rail->setObjectName(QStringLiteral("projectRail"));
    _rail->setMinimumWidth(120);
    _rail->setMaximumWidth(320);

    _projectList->setObjectName(QStringLiteral("projectList"));
    _projectList->setDefaultDropAction(Qt::MoveAction);
    _projectList->setDragDropMode(QAbstractItemView::InternalMove);
    _projectList->setDragDropOverwriteMode(false);
    _projectList->setDragEnabled(true);
    _projectList->setDropIndicatorShown(true);
    _projectList->setFrameShape(QFrame::NoFrame);
    _projectList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _projectList->setSelectionMode(QAbstractItemView::SingleSelection);
    _projectList->setContextMenuPolicy(Qt::CustomContextMenu);
    _projectList->setItemDelegate(new ProjectItemDelegate(_projectList));
    _projectList->setSpacing(2);
    connect(_projectList, &QListWidget::currentRowChanged, this, &ProjectWorkspaceContainer::currentRowChanged);
    connect(_projectList, &QListWidget::itemDoubleClicked, this, &ProjectWorkspaceContainer::renameCurrentProject);
    connect(_projectList, &QListWidget::customContextMenuRequested, this, &ProjectWorkspaceContainer::openProjectContextMenu);
    static_cast<ProjectListWidget *>(_projectList)->itemsDropped = [this] {
        QTimer::singleShot(0, this, &ProjectWorkspaceContainer::syncProjectsToListOrder);
    };
    _projectList->viewport()->setAcceptDrops(true);

    auto *railLayout = new QVBoxLayout(_rail);
    railLayout->setContentsMargins(6, 6, 6, 6);
    railLayout->setSpacing(6);
    railLayout->addWidget(_projectList, 1);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    _splitter->setChildrenCollapsible(false);
    _splitter->addWidget(_rail);
    _splitter->addWidget(_stack);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);
    _splitter->setSizes({164, 800});
    rootLayout->addWidget(_splitter);

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
    item->setData(ContainerRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(container)));
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
    item->setSizeHint(QSize(1, 56));
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

    _stack->removeWidget(container);

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

QString ProjectWorkspaceContainer::projectTitle(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return {};
    }

    return _projects.at(index).title;
}

void ProjectWorkspaceContainer::setProjectTitle(TabbedViewContainer *container, const QString &title)
{
    const int index = indexOf(container);
    if (index < 0 || title.trimmed().isEmpty()) {
        return;
    }

    _projects[index].title = title.trimmed();
    updateListItem(index);
}

QString ProjectWorkspaceContainer::projectSubtitle(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return {};
    }

    return _projects.at(index).subtitle;
}

int ProjectWorkspaceContainer::projectTabCount(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return 0;
    }

    return _projects.at(index).tabCount;
}

int ProjectWorkspaceContainer::projectActiveProcessCount(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return 0;
    }

    return _projects.at(index).activeProcessCount;
}

void ProjectWorkspaceContainer::setProjectSummary(TabbedViewContainer *container,
                                                  const QString &subtitle,
                                                  int tabCount,
                                                  int activeProcessCount,
                                                  bool hasActivity,
                                                  const QIcon &icon)
{
    const int index = indexOf(container);
    if (index < 0) {
        return;
    }

    auto &project = _projects[index];
    project.subtitle = subtitle;
    project.tabCount = tabCount;
    project.activeProcessCount = activeProcessCount;
    project.hasActivity = hasActivity;
    project.icon = icon.isNull() ? QIcon::fromTheme(QStringLiteral("folder")) : icon;
    updateListItem(index);
}

int ProjectWorkspaceContainer::projectCount() const
{
    return _projects.count();
}

QString ProjectWorkspaceContainer::nextDefaultProjectTitle() const
{
    return i18nc("@title", "Workspace %1", _nextProjectNumber);
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
    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), i18nc("@action:inmenu", "Add Workspace"), this, [this] {
        Q_EMIT newProjectRequested();
    });

    auto *renameAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu", "Rename Workspace..."), this, [this] {
        renameCurrentProject();
    });
    renameAction->setEnabled(activeContainer() != nullptr);

    auto *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close Workspace"), this, [this] {
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
                                                i18nc("@title:window", "Rename Workspace"),
                                                i18nc("@label:textbox", "Workspace name:"),
                                                QLineEdit::Normal,
                                                _projects.at(row).title,
                                                &ok);
    if (!ok || title.trimmed().isEmpty()) {
        return;
    }

    _projects[row].title = title.trimmed();
    updateListItem(row);
}

void ProjectWorkspaceContainer::syncProjectsToListOrder()
{
    if (_projectList->count() != _projects.count()) {
        qWarning("Project workspace list and container list are out of sync after drag/drop");
        return;
    }

    QList<Project> reorderedProjects;
    reorderedProjects.reserve(_projects.count());

    for (int row = 0; row < _projectList->count(); ++row) {
        auto *item = _projectList->item(row);
        if (item == nullptr) {
            qWarning("Project workspace list contains a null item after drag/drop");
            return;
        }

        auto *container = reinterpret_cast<TabbedViewContainer *>(item->data(ContainerRole).value<quintptr>());
        const int index = indexOf(container);
        if (index < 0) {
            qWarning("Project workspace list contains an unknown container after drag/drop");
            return;
        }

        reorderedProjects.append(_projects.at(index));
    }

    _projects = reorderedProjects;
    currentRowChanged(_projectList->currentRow());
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
    item->setData(SubtitleRole, _projects.at(index).subtitle);
    item->setData(TabCountRole, _projects.at(index).tabCount);
    item->setData(ActiveProcessCountRole, _projects.at(index).activeProcessCount);
    item->setData(HasActivityRole, _projects.at(index).hasActivity);
    item->setToolTip(_projects.at(index).subtitle.isEmpty() ? _projects.at(index).title
                                                            : QStringLiteral("%1\n%2").arg(_projects.at(index).title, _projects.at(index).subtitle));
    item->setIcon(_projects.at(index).icon.isNull() ? QIcon::fromTheme(QStringLiteral("folder")) : _projects.at(index).icon);
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
            padding: 3px 5px;
            border-radius: 4px;
        }
        QListWidget#projectList::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
}

#include "moc_ProjectWorkspaceContainer.cpp"
