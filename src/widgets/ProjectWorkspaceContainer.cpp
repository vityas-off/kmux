/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "widgets/ProjectWorkspaceContainer.h"

#include "widgets/ViewContainer.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDateTime>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QRegion>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <KLocalizedString>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace Konsole;

namespace
{
constexpr int ProjectItemHeight = 56;
constexpr int ProjectRailDefaultWidth = 164;
constexpr int ProjectRailMinimumWidth = 120;
constexpr int ProjectRailMaximumWidth = 320;

enum ProjectRoles {
    ProjectIdRole = Qt::UserRole,
    SubtitleRole,
    TabCountRole,
    ActiveProcessCountRole,
    HasActivityRole,
    ProjectStatusRole,
};

QString badgeText(int count)
{
    return count > 99 ? QStringLiteral("99+") : QString::number(count);
}

QColor blendedColor(const QColor &background, const QColor &foreground, qreal foregroundOpacity)
{
    const qreal backgroundOpacity = 1.0 - foregroundOpacity;
    return QColor::fromRgbF((background.redF() * backgroundOpacity) + (foreground.redF() * foregroundOpacity),
                            (background.greenF() * backgroundOpacity) + (foreground.greenF() * foregroundOpacity),
                            (background.blueF() * backgroundOpacity) + (foreground.blueF() * foregroundOpacity),
                            1.0);
}

QColor runningPulseColor(const QColor &baseColor, const QColor &highlightColor, bool selected)
{
    constexpr qint64 PulseDurationMs = 1400;
    constexpr qreal FullTurn = 6.2831853071795864769;

    const qreal progress = static_cast<qreal>(QDateTime::currentMSecsSinceEpoch() % PulseDurationMs) / PulseDurationMs;
    const qreal wave = (std::sin(progress * FullTurn) + 1.0) / 2.0;
    const qreal minimumOpacity = selected ? 0.50 : 0.34;
    const qreal maximumOpacity = selected ? 0.88 : 0.72;
    return blendedColor(baseColor, highlightColor, minimumOpacity + ((maximumOpacity - minimumOpacity) * wave));
}

int indicatorWidth(const QFontMetrics &metrics, const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }

    return 13 + 4 + metrics.horizontalAdvance(text);
}

QString projectStatusText(ProjectWorkspaceContainer::ProjectStatus status)
{
    switch (status) {
    case ProjectWorkspaceContainer::ProjectStatus::Running:
        return i18nc("@info:project status", "Running");
    case ProjectWorkspaceContainer::ProjectStatus::Idle:
        return i18nc("@info:project status", "Idle");
    case ProjectWorkspaceContainer::ProjectStatus::NeedsInput:
        return i18nc("@info:project status", "Needs input");
    case ProjectWorkspaceContainer::ProjectStatus::None:
        return {};
    }

    return {};
}

void drawTabIndicatorIcon(QPainter *painter, const QRect &rect, const QColor &color)
{
    painter->save();
    QPen pen(color, 1.4);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QRectF front(rect.left() + 3.5, rect.top() + 4.5, 7.0, 6.0);
    const QRectF back(rect.left() + 1.5, rect.top() + 2.5, 7.0, 6.0);
    painter->drawRoundedRect(back, 1.0, 1.0);
    painter->drawRoundedRect(front, 1.0, 1.0);

    painter->restore();
}

void drawProcessIndicatorIcon(QPainter *painter, const QRect &rect, const QColor &color)
{
    painter->save();
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QPoint center = rect.center();
    painter->drawEllipse(center, 3, 3);
    painter->drawLine(center.x(), rect.top() + 1, center.x(), rect.top() + 3);
    painter->drawLine(center.x(), rect.bottom() - 3, center.x(), rect.bottom() - 1);
    painter->drawLine(rect.left() + 1, center.y(), rect.left() + 3, center.y());
    painter->drawLine(rect.right() - 3, center.y(), rect.right() - 1, center.y());

    painter->restore();
}

void drawInlineIndicator(QPainter *painter,
                         const QRect &rect,
                         const QString &text,
                         const QColor &color,
                         const QFont &font,
                         void (*drawIcon)(QPainter *, const QRect &, const QColor &))
{
    if (text.isEmpty()) {
        return;
    }

    painter->save();
    painter->setPen(color);
    painter->setFont(font);

    const QRect iconRect(rect.left(), rect.top() + ((rect.height() - 13) / 2), 13, 13);
    drawIcon(painter, iconRect, color);
    painter->drawText(rect.adjusted(17, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);

    painter->restore();
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
        return {base.width(), qMax(base.height(), ProjectItemHeight)};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        QStyleOptionViewItem itemOption(option);
        initStyleOption(&itemOption, index);
        itemOption.text.clear();
        itemOption.icon = {};

        auto *style = itemOption.widget != nullptr ? itemOption.widget->style() : QApplication::style();
        QStyleOptionViewItem backgroundOption(itemOption);
        const bool selected = itemOption.state.testFlag(QStyle::State_Selected);
        backgroundOption.state.setFlag(QStyle::State_Selected, false);
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &backgroundOption, painter, itemOption.widget);

        const QRect rect = itemOption.rect.adjusted(8, 7, -12, -7);
        const QColor highlightColor = itemOption.palette.color(QPalette::Highlight);
        if (selected) {
            const QColor base = itemOption.palette.color(QPalette::Base);
            const QColor mid = itemOption.palette.color(QPalette::Mid);
            QRect backgroundRect = itemOption.rect;
            if (itemOption.widget != nullptr) {
                backgroundRect.setLeft(itemOption.widget->rect().left());
                backgroundRect.setRight(itemOption.widget->rect().right());
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(blendedColor(base, mid, 0.28));
            painter->drawRect(backgroundRect);

            painter->setBrush(highlightColor);
            painter->drawRect(QRect(backgroundRect.left(), backgroundRect.top(), 3, backgroundRect.height()));
        }

        const QColor textColor = itemOption.palette.color(QPalette::Text);
        QColor subtleColor = itemOption.palette.color(QPalette::PlaceholderText);
        if (!subtleColor.isValid()) {
            subtleColor = textColor;
        }
        subtleColor = blendedColor(subtleColor, textColor, selected ? 0.62 : 0.42);

        const int tabCount = index.data(TabCountRole).toInt();
        const int processCount = index.data(ActiveProcessCountRole).toInt();
        const bool hasActivity = index.data(HasActivityRole).toBool();
        const auto projectStatus = static_cast<ProjectWorkspaceContainer::ProjectStatus>(index.data(ProjectStatusRole).toInt());

        const QRect contentRect = rect;
        QRect titleRect = contentRect;
        QFont indicatorFont = itemOption.font;
        indicatorFont.setPointSize(qMax(1, indicatorFont.pointSize() - 1));
        const QFontMetrics indicatorMetrics(indicatorFont);
        const QString tabsBadge = tabCount > 0 ? badgeText(tabCount) : QString();
        const QString processBadge = processCount > 0 ? badgeText(processCount) : QString();
        const QString statusBadge = projectStatus == ProjectWorkspaceContainer::ProjectStatus::NeedsInput
            ? QStringLiteral("!")
            : (projectStatus == ProjectWorkspaceContainer::ProjectStatus::Running && processBadge.isEmpty() ? i18nc("@info:project status short", "run")
                                                                                                            : QString());
        const int tabsIndicatorWidth = indicatorWidth(indicatorMetrics, tabsBadge);
        const int processIndicatorWidth = indicatorWidth(indicatorMetrics, processBadge);
        const int statusIndicatorWidth = indicatorWidth(indicatorMetrics, statusBadge);
        const int visibleIndicatorCount = (!tabsBadge.isEmpty() ? 1 : 0) + (!processBadge.isEmpty() ? 1 : 0) + (!statusBadge.isEmpty() ? 1 : 0);
        const int indicatorGap = qMax(0, visibleIndicatorCount - 1) * 10;
        const int activityWidth = processBadge.isEmpty() && hasActivity ? 8 : 0;
        const int indicatorsWidth = tabsIndicatorWidth + processIndicatorWidth + statusIndicatorWidth + indicatorGap + activityWidth;
        QRect indicatorsRect;
        if (indicatorsWidth > 0) {
            indicatorsRect = QRect(rect.right() - indicatorsWidth, rect.top() + 1, indicatorsWidth, 18);
            titleRect.setRight(indicatorsRect.left() - 8);
        }

        const QFontMetrics titleMetrics(itemOption.font);
        const QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, qMax(0, titleRect.width()));

        QRect tabsIndicatorRect;
        QRect processIndicatorRect;
        QRect statusIndicatorRect;
        QRect activityRect;
        int indicatorLeft = indicatorsRect.left();
        if (!tabsBadge.isEmpty()) {
            tabsIndicatorRect = QRect(indicatorLeft, indicatorsRect.top(), tabsIndicatorWidth, indicatorsRect.height());
            indicatorLeft = tabsIndicatorRect.right() + 1 + 10;
        }
        if (!processBadge.isEmpty()) {
            processIndicatorRect = QRect(indicatorLeft, indicatorsRect.top(), processIndicatorWidth, indicatorsRect.height());
            indicatorLeft = processIndicatorRect.right() + 1 + 10;
        }
        if (!statusBadge.isEmpty()) {
            statusIndicatorRect = QRect(indicatorLeft, indicatorsRect.top(), statusIndicatorWidth, indicatorsRect.height());
        } else if (hasActivity && processBadge.isEmpty()) {
            activityRect = QRect(indicatorLeft, indicatorsRect.center().y() - 3, 7, 7);
        }

        QFont titleFont = itemOption.font;
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(textColor);
        painter->drawText(titleRect.left(), titleRect.top(), titleRect.width(), titleRect.height() / 2, Qt::AlignLeft | Qt::AlignVCenter, title);

        QFont subtitleFont = itemOption.font;
        if (!selected) {
            subtitleFont.setPointSize(qMax(1, subtitleFont.pointSize() - 1));
        }
        painter->setFont(subtitleFont);
        painter->setPen(subtleColor);
        const QFontMetrics subtitleMetrics(subtitleFont);
        const QString subtitle = subtitleMetrics.elidedText(index.data(SubtitleRole).toString(), Qt::ElideRight, qMax(0, contentRect.width()));
        painter->drawText(contentRect.left(), rect.center().y(), contentRect.width(), rect.height() / 2, Qt::AlignLeft | Qt::AlignVCenter, subtitle);

        QColor indicatorColor = blendedColor(itemOption.palette.color(QPalette::PlaceholderText), textColor, selected ? 0.75 : 0.48);
        if (!tabsIndicatorRect.isNull()) {
            drawInlineIndicator(painter, tabsIndicatorRect, tabsBadge, indicatorColor, indicatorFont, drawTabIndicatorIcon);
        }
        QColor processIndicatorColor = indicatorColor;
        if (projectStatus == ProjectWorkspaceContainer::ProjectStatus::Running) {
            processIndicatorColor = runningPulseColor(indicatorColor, highlightColor, selected);
        }
        if (!processIndicatorRect.isNull()) {
            drawInlineIndicator(painter, processIndicatorRect, processBadge, processIndicatorColor, indicatorFont, drawProcessIndicatorIcon);
        }
        if (!statusIndicatorRect.isNull()) {
            QColor statusColor = projectStatus == ProjectWorkspaceContainer::ProjectStatus::NeedsInput
                ? QColor(245, 165, 36)
                : runningPulseColor(indicatorColor, highlightColor, selected);
            drawInlineIndicator(painter, statusIndicatorRect, statusBadge, statusColor, indicatorFont, drawProcessIndicatorIcon);
        } else if (!activityRect.isNull()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(highlightColor);
            painter->drawEllipse(activityRect);
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
    , _model(new ProjectWorkspaceModel(this))
    , _rail(new QWidget(this))
    , _splitter(new QSplitter(Qt::Horizontal, this))
    , _projectList(new ProjectListWidget(this))
    , _stack(new QStackedWidget(this))
    , _statusAnimationTimer(new QTimer(this))
    , _projectRailWidth(ProjectRailDefaultWidth)
{
    connect(_model, &ProjectWorkspaceModel::projectChanged, this, [this](const ProjectWorkspaceModel::ProjectId &id) {
        updateListItem(_model->indexOf(id));
        updateStatusAnimationTimer();
    });

    _statusAnimationTimer->setInterval(100);
    connect(_statusAnimationTimer, &QTimer::timeout, _projectList->viewport(), [this] {
        QRegion runningItemsRegion;
        for (int row = 0; row < _projectList->count(); ++row) {
            auto *item = _projectList->item(row);
            if (item != nullptr && static_cast<ProjectStatus>(item->data(ProjectStatusRole).toInt()) == ProjectStatus::Running) {
                runningItemsRegion += _projectList->visualItemRect(item);
            }
        }
        _projectList->viewport()->update(runningItemsRegion);
    });

    _rail->setObjectName(QStringLiteral("projectRail"));
    _rail->setMinimumWidth(ProjectRailMinimumWidth);
    _rail->setMaximumWidth(ProjectRailMaximumWidth);

    _projectList->setObjectName(QStringLiteral("projectList"));
    _projectList->setDefaultDropAction(Qt::MoveAction);
    _projectList->setDragDropMode(QAbstractItemView::InternalMove);
    _projectList->setDragDropOverwriteMode(false);
    _projectList->setDragEnabled(true);
    _projectList->setDropIndicatorShown(true);
    _projectList->setFrameShape(QFrame::NoFrame);
    _projectList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _projectList->setFocusPolicy(Qt::NoFocus);
    _projectList->setSelectionMode(QAbstractItemView::SingleSelection);
    _projectList->setContextMenuPolicy(Qt::CustomContextMenu);
    _projectList->setItemDelegate(new ProjectItemDelegate(_projectList));
    _projectList->setSpacing(0);
    connect(_projectList, &QListWidget::currentRowChanged, this, &ProjectWorkspaceContainer::currentRowChanged);
    connect(_projectList, &QListWidget::itemDoubleClicked, this, &ProjectWorkspaceContainer::renameCurrentProject);
    connect(_projectList, &QListWidget::customContextMenuRequested, this, &ProjectWorkspaceContainer::openProjectContextMenu);
    static_cast<ProjectListWidget *>(_projectList)->itemsDropped = [this] {
        QTimer::singleShot(0, this, &ProjectWorkspaceContainer::syncProjectsToListOrder);
    };
    _projectList->viewport()->setAcceptDrops(true);

    auto *railLayout = new QVBoxLayout(_rail);
    railLayout->setContentsMargins(0, 0, 0, 0);
    railLayout->setSpacing(0);
    railLayout->addWidget(_projectList, 1);

    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    _splitter->setChildrenCollapsible(false);
    _splitter->addWidget(_rail);
    _splitter->addWidget(_stack);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);
    _splitter->setSizes({ProjectRailDefaultWidth, 800});
    connect(_splitter, &QSplitter::splitterMoved, this, [this](int position) {
        _projectRailWidth = qBound(ProjectRailMinimumWidth, position, ProjectRailMaximumWidth);
    });
    rootLayout->addWidget(_splitter);

    applyRailStyle();
}

ProjectWorkspaceContainer::~ProjectWorkspaceContainer()
{
    // Do not defer this to QObject child cleanup: destroying tab containers can synchronously query
    // the workspace model and lookup maps, which must still be alive.
    delete _stack;
    _stack = nullptr;
}

int ProjectWorkspaceContainer::addProject(TabbedViewContainer *container, const QString &title)
{
    Q_ASSERT(container != nullptr);

    const ProjectWorkspaceModel::ProjectId id = _model->addProject(title);
    _projectIds.insert(container, id);
    _containers.insert(id, container);
    const int index = _model->indexOf(id);
    _stack->addWidget(container);
    auto *item = new QListWidgetItem(_projectList);
    item->setData(ProjectIdRole, id);
    item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
    item->setSizeHint(QSize(1, ProjectItemHeight));
    updateListItem(index);
    updateStatusAnimationTimer();

    _projectList->setCurrentRow(index);
    return index;
}

void ProjectWorkspaceContainer::removeProject(TabbedViewContainer *container)
{
    const int index = indexOf(container);
    if (index < 0) {
        return;
    }
    auto *previousActiveContainer = activeContainer();
    const bool removingActiveProject = previousActiveContainer == container;

    _stack->removeWidget(container);

    {
        const QSignalBlocker blocker(_projectList);
        delete _projectList->takeItem(index);
        const ProjectWorkspaceModel::ProjectId id = projectId(container);
        _projectIds.remove(container);
        _containers.remove(id);
        _model->removeProject(id);

        if (_model->projectCount() > 0) {
            const int nextIndex = removingActiveProject ? qMin(index, _model->projectCount() - 1) : indexOf(previousActiveContainer);
            _projectList->setCurrentRow(nextIndex);
        }
    }
    updateStatusAnimationTimer();

    if (_model->projectCount() == 0) {
        return;
    }

    currentRowChanged(_projectList->currentRow());
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
    return containerAt(row);
}

QList<TabbedViewContainer *> ProjectWorkspaceContainer::containers() const
{
    QList<TabbedViewContainer *> result;
    result.reserve(_model->projectCount());
    for (const ProjectWorkspaceModel::ProjectId &id : _model->projectIds()) {
        result.append(_containers.value(id));
    }
    return result;
}

TabbedViewContainer *ProjectWorkspaceContainer::containerForWidget(QWidget *widget) const
{
    if (widget == nullptr) {
        return nullptr;
    }

    for (TabbedViewContainer *container : containers()) {
        if (container == widget || container->isAncestorOf(widget)) {
            return container;
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

    return _model->projectAt(index).title;
}

void ProjectWorkspaceContainer::setProjectTitle(TabbedViewContainer *container, const QString &title)
{
    const int index = indexOf(container);
    if (index < 0 || title.trimmed().isEmpty()) {
        return;
    }

    _model->setProjectTitle(projectId(container), title);
}

QString ProjectWorkspaceContainer::projectSubtitle(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return {};
    }

    return _model->projectAt(index).subtitle;
}

int ProjectWorkspaceContainer::projectTabCount(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return 0;
    }

    return _model->projectAt(index).tabCount;
}

int ProjectWorkspaceContainer::projectActiveProcessCount(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return 0;
    }

    return _model->projectAt(index).activeProcessCount;
}

bool ProjectWorkspaceContainer::projectHasActivity(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return false;
    }

    return _model->projectAt(index).hasActivity;
}

ProjectWorkspaceContainer::ProjectStatus ProjectWorkspaceContainer::projectStatus(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return ProjectStatus::None;
    }

    return _model->projectAt(index).status;
}

QString ProjectWorkspaceContainer::projectNotification(TabbedViewContainer *container) const
{
    const int index = indexOf(container);
    if (index < 0) {
        return {};
    }

    return _model->projectAt(index).notification;
}

void ProjectWorkspaceContainer::setProjectNotification(TabbedViewContainer *container, const QString &notification)
{
    const int index = indexOf(container);
    if (index < 0) {
        return;
    }

    _model->setProjectNotification(projectId(container), notification);
}

void ProjectWorkspaceContainer::setProjectSummary(TabbedViewContainer *container,
                                                  const QString &subtitle,
                                                  int tabCount,
                                                  int activeProcessCount,
                                                  bool hasActivity,
                                                  ProjectStatus status,
                                                  const QIcon &icon)
{
    const int index = indexOf(container);
    if (index < 0) {
        return;
    }

    _model->setProjectSummary(projectId(container), subtitle, tabCount, activeProcessCount, hasActivity, status, icon);
}

int ProjectWorkspaceContainer::projectCount() const
{
    return _model->projectCount();
}

QString ProjectWorkspaceContainer::nextDefaultProjectTitle() const
{
    int projectNumber = _model->nextProjectNumber();
    QString title;
    bool titleInUse = false;
    do {
        title = i18nc("@title", "Project %1", projectNumber++);
        titleInUse = false;
        for (int projectIndex = 0; projectIndex < _model->projectCount(); ++projectIndex) {
            if (_model->projectAt(projectIndex).title == title) {
                titleInUse = true;
                break;
            }
        }
    } while (titleInUse);
    return title;
}

ProjectWorkspaceModel *ProjectWorkspaceContainer::projectModel() const
{
    return _model;
}

void ProjectWorkspaceContainer::setProjectNavigationVisible(bool visible)
{
    _rail->setVisible(visible);
}

int ProjectWorkspaceContainer::projectRailWidth() const
{
    return qBound(ProjectRailMinimumWidth, _projectRailWidth, ProjectRailMaximumWidth);
}

void ProjectWorkspaceContainer::setProjectRailWidth(int requestedWidth)
{
    const int railWidth = qBound(ProjectRailMinimumWidth, requestedWidth, ProjectRailMaximumWidth);
    _projectRailWidth = railWidth;
    const QList<int> sizes = _splitter->sizes();
    const int contentWidth = sizes.count() > 1 ? qMax(1, sizes.at(1)) : qMax(1, width() - railWidth);
    _splitter->setSizes({railWidth, contentWidth});
}

void ProjectWorkspaceContainer::currentRowChanged(int row)
{
    auto *container = containerAt(row);
    if (container == nullptr) {
        return;
    }

    _stack->setCurrentWidget(container);
    Q_EMIT currentProjectChanged(container);
}

void ProjectWorkspaceContainer::openProjectContextMenu(const QPoint &point)
{
    auto *item = _projectList->itemAt(point);
    if (item != nullptr) {
        _projectList->setCurrentItem(item);
    }

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), i18nc("@action:inmenu", "Add Project"), this, [this] {
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
    if (row < 0 || row >= _model->projectCount()) {
        return;
    }

    bool ok = false;
    const QString title = QInputDialog::getText(this,
                                                i18nc("@title:window", "Rename Project"),
                                                i18nc("@label:textbox", "Project name:"),
                                                QLineEdit::Normal,
                                                _model->projectAt(row).title,
                                                &ok);
    if (!ok || title.trimmed().isEmpty()) {
        return;
    }

    _model->setProjectTitle(_model->projectAt(row).id, title);
}

void ProjectWorkspaceContainer::syncProjectsToListOrder()
{
    if (_projectList->count() != _model->projectCount()) {
        qWarning("Project workspace list and container list are out of sync after drag/drop");
        return;
    }

    QList<ProjectWorkspaceModel::ProjectId> orderedIds;
    orderedIds.reserve(_model->projectCount());

    for (int row = 0; row < _projectList->count(); ++row) {
        auto *item = _projectList->item(row);
        if (item == nullptr) {
            qWarning("Project workspace list contains a null item after drag/drop");
            return;
        }

        const ProjectWorkspaceModel::ProjectId id = item->data(ProjectIdRole).toUuid();
        if (!_containers.contains(id)) {
            qWarning("Project workspace list contains an unknown container after drag/drop");
            return;
        }

        orderedIds.append(id);
    }

    if (!_model->reorderProjects(orderedIds)) {
        qWarning("Project workspace model rejected the project order after drag/drop");
        return;
    }
    currentRowChanged(_projectList->currentRow());
}

int ProjectWorkspaceContainer::indexOf(TabbedViewContainer *container) const
{
    return _model->indexOf(projectId(container));
}

ProjectWorkspaceModel::ProjectId ProjectWorkspaceContainer::projectId(TabbedViewContainer *container) const
{
    return _projectIds.value(container);
}

TabbedViewContainer *ProjectWorkspaceContainer::containerAt(int index) const
{
    if (_model == nullptr || index < 0 || index >= _model->projectCount()) {
        return nullptr;
    }

    return _containers.value(_model->projectAt(index).id);
}

void ProjectWorkspaceContainer::updateListItem(int index)
{
    if (index < 0 || index >= _model->projectCount()) {
        return;
    }

    auto *item = _projectList->item(index);
    if (item == nullptr) {
        return;
    }

    const ProjectWorkspaceModel::ProjectData project = _model->projectAt(index);
    item->setText(project.title);
    item->setData(SubtitleRole, project.subtitle);
    item->setData(TabCountRole, project.tabCount);
    item->setData(ActiveProcessCountRole, project.activeProcessCount);
    item->setData(HasActivityRole, project.hasActivity);
    item->setData(ProjectStatusRole, static_cast<int>(project.status));
    QString tooltip = project.subtitle.isEmpty() ? project.title : QStringLiteral("%1\n%2").arg(project.title, project.subtitle);
    const QString statusText = projectStatusText(project.status);
    if (!statusText.isEmpty()) {
        tooltip += QStringLiteral("\n%1").arg(statusText);
    }
    if (!project.notification.isEmpty()) {
        tooltip += QStringLiteral("\n%1").arg(i18nc("@info:tooltip", "Last notification: %1", project.notification));
    }
    item->setToolTip(tooltip);
    item->setIcon(project.icon.isNull() ? QIcon::fromTheme(QStringLiteral("folder")) : project.icon);
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
            padding: 0;
            border-radius: 4px;
        }
        QListWidget#projectList::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
}

void ProjectWorkspaceContainer::updateStatusAnimationTimer()
{
    const bool hasRunningProject = _model->hasRunningProject();

    if (hasRunningProject && !_statusAnimationTimer->isActive()) {
        _statusAnimationTimer->start();
    } else if (!hasRunningProject && _statusAnimationTimer->isActive()) {
        _statusAnimationTimer->stop();
    }
}

#include "moc_ProjectWorkspaceContainer.cpp"
