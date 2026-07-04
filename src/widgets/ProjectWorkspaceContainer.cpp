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
constexpr int ProjectItemHeight = 56;

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

QColor blendedColor(const QColor &background, const QColor &foreground, qreal foregroundOpacity)
{
    const qreal backgroundOpacity = 1.0 - foregroundOpacity;
    return QColor::fromRgbF((background.redF() * backgroundOpacity) + (foreground.redF() * foregroundOpacity),
                            (background.greenF() * backgroundOpacity) + (foreground.greenF() * foregroundOpacity),
                            (background.blueF() * backgroundOpacity) + (foreground.blueF() * foregroundOpacity),
                            1.0);
}

int indicatorWidth(const QFontMetrics &metrics, const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }

    return 13 + 4 + metrics.horizontalAdvance(text);
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

        const QRect contentRect = rect;
        QRect titleRect = contentRect;
        QFont indicatorFont = itemOption.font;
        indicatorFont.setPointSize(qMax(1, indicatorFont.pointSize() - 1));
        const QFontMetrics indicatorMetrics(indicatorFont);
        const QString tabsBadge = tabCount > 0 ? badgeText(tabCount) : QString();
        const QString processBadge = processCount > 0 ? badgeText(processCount) : QString();
        const int tabsIndicatorWidth = indicatorWidth(indicatorMetrics, tabsBadge);
        const int processIndicatorWidth = indicatorWidth(indicatorMetrics, processBadge);
        const int indicatorGap = (!tabsBadge.isEmpty() && !processBadge.isEmpty()) ? 10 : 0;
        const int activityWidth = processBadge.isEmpty() && hasActivity ? 8 : 0;
        const int indicatorsWidth = tabsIndicatorWidth + processIndicatorWidth + indicatorGap + activityWidth;
        QRect indicatorsRect;
        if (indicatorsWidth > 0) {
            indicatorsRect = QRect(rect.right() - indicatorsWidth, rect.top() + 1, indicatorsWidth, 18);
            titleRect.setRight(indicatorsRect.left() - 8);
        }

        const QFontMetrics titleMetrics(itemOption.font);
        const QString title = titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, qMax(0, titleRect.width()));

        QRect tabsIndicatorRect;
        QRect processIndicatorRect;
        QRect activityRect;
        int indicatorLeft = indicatorsRect.left();
        if (!tabsBadge.isEmpty()) {
            tabsIndicatorRect = QRect(indicatorLeft, indicatorsRect.top(), tabsIndicatorWidth, indicatorsRect.height());
            indicatorLeft = tabsIndicatorRect.right() + 1 + indicatorGap;
        }
        if (!processBadge.isEmpty()) {
            processIndicatorRect = QRect(indicatorLeft, indicatorsRect.top(), processIndicatorWidth, indicatorsRect.height());
        } else if (hasActivity) {
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
        if (!processIndicatorRect.isNull()) {
            drawInlineIndicator(painter, processIndicatorRect, processBadge, indicatorColor, indicatorFont, drawProcessIndicatorIcon);
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
    item->setSizeHint(QSize(1, ProjectItemHeight));
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
            padding: 0;
            border-radius: 4px;
        }
        QListWidget#projectList::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
}

#include "moc_ProjectWorkspaceContainer.cpp"
