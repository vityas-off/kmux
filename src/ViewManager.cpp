/*
    SPDX-FileCopyrightText: 2006-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ViewManager.h"

#include "config-konsole.h"
#include "konsoledebug.h"

// Qt
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSet>
#include <QStringList>
#include <QTabBar>

#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>

#include <algorithm>
#include <cerrno>
#include <limits>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

#if HAVE_DBUS
#include <QDBusArgument>
#include <QDBusMetaType>
#endif

// KDE
#include <KActionCollection>
#include <KActionMenu>
#include <KConfigGroup>
#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

// Konsole
#if HAVE_DBUS
#include <windowadaptor.h>
#endif

#include "colorscheme/ColorScheme.h"
#include "colorscheme/ColorSchemeManager.h"

#include "profile/ProfileManager.h"

#include "containers/ContainerRegistry.h"
#include "containers/ContainerSessionState.h"

#include "session/Session.h"
#include "session/SessionController.h"
#include "session/SessionManager.h"

#include "terminalDisplay/TerminalDisplay.h"
#include "widgets/ProjectWorkspaceContainer.h"
#include "widgets/ViewContainer.h"
#include "widgets/ViewSplitter.h"

using namespace Konsole;

int ViewManager::lastManagerId = 0;

Q_DECLARE_METATYPE(QList<double>);

namespace
{
constexpr int ProjectStatusProcessCheckIntervalMs = 2000;

bool projectStatusProcessIsAlive(qlonglong processId)
{
#ifdef Q_OS_UNIX
    if (processId <= 0 || processId > std::numeric_limits<pid_t>::max()) {
        return false;
    }

    errno = 0;
    return ::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#else
    Q_UNUSED(processId)
    return true;
#endif
}

ProjectWorkspaceContainer::ProjectStatus projectStatusFromString(const QString &status)
{
    QString normalized = status.trimmed().toLower();
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char(' '));

    if (normalized == QLatin1String("running") || normalized == QLatin1String("working") || normalized == QLatin1String("busy")) {
        return ProjectWorkspaceContainer::ProjectStatus::Running;
    }

    if (normalized == QLatin1String("idle") || normalized == QLatin1String("complete") || normalized == QLatin1String("done")) {
        return ProjectWorkspaceContainer::ProjectStatus::Idle;
    }

    if (normalized == QLatin1String("needsinput") || normalized == QLatin1String("waiting") || normalized == QLatin1String("waitingforinput")) {
        return ProjectWorkspaceContainer::ProjectStatus::NeedsInput;
    }

    return ProjectWorkspaceContainer::ProjectStatus::None;
}

ProjectWorkspaceContainer::ProjectStatus higherPriorityProjectStatus(ProjectWorkspaceContainer::ProjectStatus current,
                                                                     ProjectWorkspaceContainer::ProjectStatus candidate)
{
    auto priority = [](ProjectWorkspaceContainer::ProjectStatus status) {
        switch (status) {
        case ProjectWorkspaceContainer::ProjectStatus::NeedsInput:
            return 3;
        case ProjectWorkspaceContainer::ProjectStatus::Running:
            return 2;
        case ProjectWorkspaceContainer::ProjectStatus::Idle:
            return 1;
        case ProjectWorkspaceContainer::ProjectStatus::None:
            return 0;
        }

        return 0;
    };

    return priority(candidate) > priority(current) ? candidate : current;
}
}

ViewManager::ViewManager(QObject *parent, KActionCollection *collection)
    : QObject(parent)
    , _viewContainer(nullptr)
    , _workspaceContainer(nullptr)
    , _pluggedController(nullptr)
    , _sessionMap(QHash<TerminalDisplay *, Session *>())
    , _actionCollection(collection)
    , _navigationMethod(TabbedNavigation)
    , _navigationVisibility(NavigationNotSet)
    , _managerId(0)
    , _terminalDisplayHistoryIndex(-1)
    , contextMenuAdditionalActions({})
{
#if HAVE_DBUS
    qDBusRegisterMetaType<QList<double>>();
#endif

    _workspaceContainer = new ProjectWorkspaceContainer();
    _viewContainer = createContainer();
    _workspaceContainer->addProject(_viewContainer, _workspaceContainer->nextDefaultProjectTitle());

    connect(_workspaceContainer, &ProjectWorkspaceContainer::newProjectRequested, this, &ViewManager::createProject);
    connect(_workspaceContainer, &ProjectWorkspaceContainer::closeProjectRequested, this, &ViewManager::closeProject);
    connect(_workspaceContainer, &ProjectWorkspaceContainer::currentProjectChanged, this, &ViewManager::activeProjectChanged);
    connect(_workspaceContainer->projectModel(), &ProjectWorkspaceModel::projectChanged, this, &ViewManager::updateProjectInputRequirement);
    connect(_workspaceContainer->projectModel(), &ProjectWorkspaceModel::projectRemoved, this, &ViewManager::updateProjectInputRequirement);

    _projectStatusProcessTimer.setInterval(ProjectStatusProcessCheckIntervalMs);
    connect(&_projectStatusProcessTimer, &QTimer::timeout, this, &ViewManager::clearExitedSessionProjectStatuses);

    // setup actions which are related to the views
    setupActions();

    // listen for profile changes
    connect(ProfileManager::instance(), &Konsole::ProfileManager::profileChanged, this, &Konsole::ViewManager::profileChanged);
    connect(SessionManager::instance(), &Konsole::SessionManager::sessionUpdated, this, &Konsole::ViewManager::updateViewsForSession);

    _managerId = ++lastManagerId;

#if HAVE_DBUS
    // prepare DBus communication
    new WindowAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QLatin1String("/Windows/") + QString::number(_managerId), this);
#endif
}

ViewManager::~ViewManager() = default;

int ViewManager::managerId() const
{
    return _managerId;
}

bool ViewManager::hasProjectNeedingInput() const
{
    return _hasProjectNeedingInput;
}

QWidget *ViewManager::activeView() const
{
    return activeContainer() != nullptr ? activeContainer()->currentWidget() : nullptr;
}

QWidget *ViewManager::widget() const
{
    return _workspaceContainer;
}

void ViewManager::setupActions()
{
    Q_ASSERT(_actionCollection);
    if (_actionCollection == nullptr) {
        return;
    }

    KActionCollection *collection = _actionCollection;
    KActionMenu *splitViewActions =
        new KActionMenu(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18nc("@action:inmenu", "Split View"), collection);
    splitViewActions->setPopupMode(QToolButton::InstantPopup);
    collection->addAction(QStringLiteral("split-view"), splitViewActions);

    // Let's reuse the pointer, no need not to.
    auto *action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
    action->setText(i18nc("@action:inmenu", "Add Project"));
    connect(action, &QAction::triggered, this, &ViewManager::createProject);
    collection->addAction(QStringLiteral("add-workspace"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_W);
    _workspaceContainer->addAction(action);

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    action->setText(i18nc("@action:inmenu", "Split View Left/Right"));
    connect(action, &QAction::triggered, this, &ViewManager::splitLeftRight);
    collection->addAction(QStringLiteral("split-view-left-right"), action);
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::Key_ParenLeft));
    splitViewActions->addAction(action);

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")));
    action->setText(i18nc("@action:inmenu", "Split View Top/Bottom"));
    connect(action, &QAction::triggered, this, &ViewManager::splitTopBottom);
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::Key_ParenRight));
    collection->addAction(QStringLiteral("split-view-top-bottom"), action);
    splitViewActions->addAction(action);

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-auto")));
    action->setText(i18nc("@action:inmenu", "Split View Automatically"));
    connect(action, &QAction::triggered, this, &ViewManager::splitAuto);
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::Key_Asterisk));
    collection->addAction(QStringLiteral("split-view-auto"), action);
    splitViewActions->addAction(action);

    splitViewActions->addSeparator();

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    action->setText(i18nc("@action:inmenu", "Split View Left/Right from next tab"));
    connect(action, &QAction::triggered, this, &ViewManager::splitLeftRightNextTab);
    collection->addAction(QStringLiteral("split-view-left-right-next-tab"), action);
    splitViewActions->addAction(action);
    _multiTabOnlyActions << action;

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")));
    action->setText(i18nc("@action:inmenu", "Split View Top/Bottom from next tab"));
    connect(action, &QAction::triggered, this, &ViewManager::splitTopBottomNextTab);
    collection->addAction(QStringLiteral("split-view-top-bottom-next-tab"), action);
    splitViewActions->addAction(action);
    _multiTabOnlyActions << action;

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-auto")));
    action->setText(i18nc("@action:inmenu", "Split View Automatically from next tab"));
    connect(action, &QAction::triggered, this, &ViewManager::splitAutoNextTab);
    collection->addAction(QStringLiteral("split-view-auto-next-tab"), action);
    splitViewActions->addAction(action);
    _multiTabOnlyActions << action;

    splitViewActions->addSeparator();

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")));
    action->setText(i18nc("@action:inmenu", "Load a new tab with layout 2x2 terminals"));
    connect(action, &QAction::triggered, this, [this]() {
        this->loadLayout(QStringLiteral(":/kmux/layouts/2x2-terminals.json"));
    });
    collection->addAction(QStringLiteral("load-terminals-layout-2x2"), action);
    splitViewActions->addAction(action);

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    action->setText(i18nc("@action:inmenu", "Load a new tab with layout 2x1 terminals"));
    connect(action, &QAction::triggered, this, [this]() {
        this->loadLayout(QStringLiteral(":/kmux/layouts/2x1-terminals.json"));
    });
    collection->addAction(QStringLiteral("load-terminals-layout-2x1"), action);
    splitViewActions->addAction(action);

    action = new QAction(this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")));
    action->setText(i18nc("@action:inmenu", "Load a new tab with layout 1x2 terminals"));
    connect(action, &QAction::triggered, this, [this]() {
        this->loadLayout(QStringLiteral(":/kmux/layouts/1x2-terminals.json"));
    });
    collection->addAction(QStringLiteral("load-terminals-layout-1x2"), action);
    splitViewActions->addAction(action);

    action = new QAction(this);
    action->setText(i18nc("@action:inmenu", "Expand View"));
    action->setEnabled(false);
    connect(action, &QAction::triggered, this, &ViewManager::expandActiveContainer);
    collection->setDefaultShortcut(action, static_cast<Qt::Modifiers>(Konsole::ACCEL) | Qt::Key_BracketRight);
    collection->addAction(QStringLiteral("expand-active-view"), action);
    _multiSplitterOnlyActions << action;

    action = new QAction(this);
    action->setText(i18nc("@action:inmenu", "Shrink View"));
    collection->setDefaultShortcut(action, static_cast<Qt::Modifiers>(Konsole::ACCEL) | Qt::Key_BracketLeft);
    action->setEnabled(false);
    collection->addAction(QStringLiteral("shrink-active-view"), action);
    connect(action, &QAction::triggered, this, &ViewManager::shrinkActiveContainer);
    _multiSplitterOnlyActions << action;

    action = collection->addAction(QStringLiteral("detach-view"));
    action->setEnabled(false);
    action->setVisible(false);
    action->setIcon(QIcon::fromTheme(QStringLiteral("tab-detach")));
    action->setText(i18nc("@action:inmenu", "Detach Current &View"));

    action = collection->addAction(QStringLiteral("detach-tab"));
    action->setEnabled(false);
    action->setVisible(false);
    action->setIcon(QIcon::fromTheme(QStringLiteral("tab-detach")));
    action->setText(i18nc("@action:inmenu", "Detach Current &Tab"));

    // keyboard shortcut only actions
    action = new QAction(i18nc("@action Shortcut entry", "Next Tab"), this);
    const QList<QKeySequence> nextViewActionKeys{QKeySequence{Qt::SHIFT | Qt::Key_Right}, QKeySequence{Qt::CTRL | Qt::Key_PageDown}};
    collection->setDefaultShortcuts(action, nextViewActionKeys);
    collection->addAction(QStringLiteral("next-tab"), action);
    connect(action, &QAction::triggered, this, &ViewManager::nextView);
    _multiTabOnlyActions << action;
    // _viewSplitter->addAction(nextViewAction);

    action = new QAction(i18nc("@action Shortcut entry", "Previous Tab"), this);
    const QList<QKeySequence> previousViewActionKeys{QKeySequence{Qt::SHIFT | Qt::Key_Left}, QKeySequence{Qt::CTRL | Qt::Key_PageUp}};
    collection->setDefaultShortcuts(action, previousViewActionKeys);
    collection->addAction(QStringLiteral("previous-tab"), action);
    connect(action, &QAction::triggered, this, &ViewManager::previousView);
    _multiTabOnlyActions << action;
    // _viewSplitter->addAction(previousViewAction);

    action = new QAction(i18nc("@action Shortcut entry", "Next Project"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_PageDown);
    collection->addAction(QStringLiteral("next-workspace"), action);
    connect(action, &QAction::triggered, this, &ViewManager::nextProject);
    _multiProjectOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Previous Project"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_PageUp);
    collection->addAction(QStringLiteral("previous-workspace"), action);
    connect(action, &QAction::triggered, this, &ViewManager::previousProject);
    _multiProjectOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Next Project Needing Attention"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_A);
    collection->addAction(QStringLiteral("next-attention-workspace"), action);
    connect(action, &QAction::triggered, this, &ViewManager::nextProjectNeedingAttention);
    _multiProjectOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Focus Above Terminal"), this);
    connect(action, &QAction::triggered, this, &ViewManager::focusUp);
    collection->addAction(QStringLiteral("focus-view-above"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Up);
    _workspaceContainer->addAction(action);
    _multiSplitterOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Focus Below Terminal"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Down);
    collection->addAction(QStringLiteral("focus-view-below"), action);
    connect(action, &QAction::triggered, this, &ViewManager::focusDown);
    _multiSplitterOnlyActions << action;
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Focus Left Terminal"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Left);
    connect(action, &QAction::triggered, this, &ViewManager::focusLeft);
    collection->addAction(QStringLiteral("focus-view-left"), action);
    _multiSplitterOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Focus Right Terminal"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Right);
    connect(action, &QAction::triggered, this, &ViewManager::focusRight);
    collection->addAction(QStringLiteral("focus-view-right"), action);
    _multiSplitterOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Focus Next Terminal"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::Key_F11);
    connect(action, &QAction::triggered, this, &ViewManager::focusNext);
    collection->addAction(QStringLiteral("focus-view-next"), action);
    _multiSplitterOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Focus Previous Terminal"), this);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_F11);
    connect(action, &QAction::triggered, this, &ViewManager::focusPrev);
    collection->addAction(QStringLiteral("focus-view-prev"), action);
    _multiSplitterOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Switch to Last Tab"), this);
    connect(action, &QAction::triggered, this, &ViewManager::lastView);
    collection->addAction(QStringLiteral("last-tab"), action);
    _multiTabOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Last Used Tabs"), this);
    connect(action, &QAction::triggered, this, &ViewManager::lastUsedView);
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::Key_Tab));
    collection->addAction(QStringLiteral("last-used-tab"), action);

    action = new QAction(i18nc("@action Shortcut entry", "Toggle Between Two Tabs"), this);
    connect(action, &QAction::triggered, this, &Konsole::ViewManager::toggleTwoViews);
    collection->addAction(QStringLiteral("toggle-two-tabs"), action);
    _multiTabOnlyActions << action;

    action = new QAction(i18nc("@action Shortcut entry", "Last Used Tabs (Reverse)"), this);
    collection->addAction(QStringLiteral("last-used-tab-reverse"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Tab);
    connect(action, &QAction::triggered, this, &ViewManager::lastUsedViewReverse);

    action = new QAction(i18nc("@action Shortcut entry", "Toggle maximize current view"), this);
    action->setText(i18nc("@action:inmenu", "Toggle maximize current view"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
    collection->addAction(QStringLiteral("toggle-maximize-current-view"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_E);
    connect(action, &QAction::triggered, this, [this] {
        if (auto *container = activeContainer()) {
            container->toggleMaximizeCurrentTerminal();
        }
    });
    _multiSplitterOnlyActions << action;
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Toggle zoom-maximize current view"), this);
    action->setText(i18nc("@action:inmenu", "Toggle zoom-maximize current view"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
    collection->addAction(QStringLiteral("toggle-zoom-current-view"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::SHIFT | Qt::Key_Z);
    connect(action, &QAction::triggered, this, [this] {
        if (auto *container = activeContainer()) {
            container->toggleZoomMaximizeCurrentTerminal();
        }
    });
    _multiSplitterOnlyActions << action;
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Move tab to the right"), this);
    collection->addAction(QStringLiteral("move-tab-to-right"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_Right);
    connect(action, &QAction::triggered, this, [this] {
        if (auto *container = activeContainer()) {
            container->moveTabRight();
        }
    });
    _multiTabOnlyActions << action;
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Move tab to the left"), this);
    collection->addAction(QStringLiteral("move-tab-to-left"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_Left);
    connect(action, &QAction::triggered, this, [this] {
        if (auto *container = activeContainer()) {
            container->moveTabLeft();
        }
    });
    _multiTabOnlyActions << action;
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Setup semantic integration (bash)"), this);
    collection->addAction(QStringLiteral("semantic-setup-bash"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_BracketRight);
    connect(action, &QAction::triggered, this, &ViewManager::semanticSetupBash);
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Toggle semantic hints display"), this);
    collection->addAction(QStringLiteral("toggle-semantic-hints"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_BracketLeft);
    connect(action, &QAction::triggered, this, &ViewManager::toggleSemanticHints);
    _workspaceContainer->addAction(action);

    action = new QAction(i18nc("@action Shortcut entry", "Toggle line numbers display"), this);
    collection->addAction(QStringLiteral("toggle-line-numbers"), action);
    collection->setDefaultShortcut(action, Qt::CTRL | Qt::ALT | Qt::Key_Backslash);
    connect(action, &QAction::triggered, this, &ViewManager::toggleLineNumbers);
    _workspaceContainer->addAction(action);

    action = new QAction(this);
    action->setText(i18nc("@action:inmenu", "Equal size to all views"));
    collection->setDefaultShortcut(action, static_cast<Qt::Modifiers>(Konsole::ACCEL) | Qt::SHIFT | Qt::Key_Backslash);
    action->setEnabled(false);
    collection->addAction(QStringLiteral("equal-size-view"), action);
    connect(action, &QAction::triggered, this, &ViewManager::equalSizeAllContainers);
    _multiSplitterOnlyActions << action;

    // _viewSplitter->addAction(lastUsedViewReverseAction);
    const int SWITCH_TO_TAB_COUNT = 19;
    for (int i = 0; i < SWITCH_TO_TAB_COUNT; ++i) {
        action = new QAction(i18nc("@action Shortcut entry", "Switch to Tab %1", i + 1), this);
        connect(action, &QAction::triggered, this, [this, i]() {
            switchToView(i);
        });
        collection->addAction(QStringLiteral("switch-to-tab-%1").arg(i), action);
        _multiTabOnlyActions << action;

        // only add default shortcut bindings for the first 10 tabs, regardless of SWITCH_TO_TAB_COUNT
        if (i < 9) {
            collection->setDefaultShortcut(action, QStringLiteral("Alt+%1").arg(i + 1));
        } else if (i == 9) {
            // add shortcut for 10th tab
            collection->setDefaultShortcut(action, QKeySequence(Qt::ALT | Qt::Key_0));
        }
    }

    const int SWITCH_TO_PROJECT_COUNT = 9;
    for (int i = 0; i < SWITCH_TO_PROJECT_COUNT; ++i) {
        action = new QAction(i18nc("@action Shortcut entry", "Switch to Project %1", i + 1), this);
        connect(action, &QAction::triggered, this, [this, i]() {
            switchToProject(i);
        });
        collection->addAction(QStringLiteral("switch-to-workspace-%1").arg(i), action);
        collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::ALT | (Qt::Key_1 + i)));
        _multiProjectOnlyActions << action;
    }

    toggleActionsBasedOnState();
}

void ViewManager::toggleActionsBasedOnState()
{
    auto *container = activeContainer();
    const int count = container != nullptr ? container->count() : 0;
    for (QAction *tabOnlyAction : std::as_const(_multiTabOnlyActions)) {
        tabOnlyAction->setEnabled(count > 1);
    }

    const int projectCount = _workspaceContainer != nullptr ? _workspaceContainer->projectCount() : 0;
    for (QAction *projectOnlyAction : std::as_const(_multiProjectOnlyActions)) {
        projectOnlyAction->setEnabled(_navigationMethod != NoNavigation && projectCount > 1);
    }

    if ((container != nullptr) && (container->activeViewSplitter() != nullptr)) {
        const int splitCount = container->activeViewSplitter()->getToplevelSplitter()->findChildren<TerminalDisplay *>().count();

        for (QAction *action : std::as_const(_multiSplitterOnlyActions)) {
            action->setEnabled(splitCount > 1);
        }
    }
}

void ViewManager::switchToView(int index)
{
    if (auto *container = activeContainer()) {
        container->setCurrentIndex(index);
    }
}

void ViewManager::switchToProject(int index)
{
    if (_workspaceContainer == nullptr || index < 0) {
        return;
    }

    const auto containers = _workspaceContainer->containers();
    if (index >= containers.count()) {
        return;
    }

    _workspaceContainer->activateProject(containers.at(index));
}

void ViewManager::switchToTerminalDisplay(Konsole::TerminalDisplay *terminalDisplay)
{
    if (auto *container = containerForTerminal(terminalDisplay)) {
        _workspaceContainer->activateProject(container);
    }

    auto *container = activeContainer();
    if (container == nullptr) {
        return;
    }

    auto splitter = ViewSplitter::parentSplitterForDisplay(terminalDisplay);
    auto toplevelSplitter = splitter->getToplevelSplitter();

    // Focus the TermialDisplay
    terminalDisplay->setFocus();

    if (container->currentWidget() != toplevelSplitter) {
        // Focus the tab
        switchToView(container->indexOf(toplevelSplitter));
    }
}

void ViewManager::focusUp()
{
    activeContainer()->activeViewSplitter()->focusUp();
}

void ViewManager::focusDown()
{
    activeContainer()->activeViewSplitter()->focusDown();
}

void ViewManager::focusLeft()
{
    activeContainer()->activeViewSplitter()->focusLeft();
}

void ViewManager::focusRight()
{
    activeContainer()->activeViewSplitter()->focusRight();
}

void ViewManager::focusNext()
{
    activeContainer()->activeViewSplitter()->focusNext();
}

void ViewManager::focusPrev()
{
    activeContainer()->activeViewSplitter()->focusPrev();
}

void ViewManager::moveActiveViewLeft()
{
    activeContainer()->moveActiveView(TabbedViewContainer::MoveViewLeft);
}

void ViewManager::moveActiveViewRight()
{
    activeContainer()->moveActiveView(TabbedViewContainer::MoveViewRight);
}

void ViewManager::nextContainer()
{
    //    _viewSplitter->activateNextContainer();
}

void ViewManager::nextProject()
{
    if (_workspaceContainer == nullptr || _workspaceContainer->projectCount() <= 1) {
        return;
    }

    const auto containers = _workspaceContainer->containers();
    const int currentIndex = containers.indexOf(activeContainer());
    if (currentIndex < 0) {
        return;
    }

    switchToProject((currentIndex + 1) % containers.count());
}

void ViewManager::previousProject()
{
    if (_workspaceContainer == nullptr || _workspaceContainer->projectCount() <= 1) {
        return;
    }

    const auto containers = _workspaceContainer->containers();
    const int currentIndex = containers.indexOf(activeContainer());
    if (currentIndex < 0) {
        return;
    }

    switchToProject((currentIndex + containers.count() - 1) % containers.count());
}

void ViewManager::nextProjectNeedingAttention()
{
    if (_workspaceContainer == nullptr || _workspaceContainer->projectCount() <= 1) {
        return;
    }

    const auto containers = _workspaceContainer->containers();
    const int currentIndex = containers.indexOf(activeContainer());
    if (currentIndex < 0) {
        return;
    }

    for (int offset = 1; offset < containers.count(); ++offset) {
        const int projectIndex = (currentIndex + offset) % containers.count();
        auto *container = containers.at(projectIndex);
        if (_workspaceContainer->projectHasActivity(container)
            || _workspaceContainer->projectStatus(container) == ProjectWorkspaceContainer::ProjectStatus::NeedsInput) {
            switchToProject(projectIndex);
            return;
        }
    }
}

void ViewManager::nextView()
{
    activeContainer()->activateNextView();
}

void ViewManager::previousView()
{
    activeContainer()->activatePreviousView();
}

void ViewManager::lastView()
{
    activeContainer()->activateLastView();
}

void ViewManager::activateLastUsedView(bool reverse)
{
    if (_terminalDisplayHistory.count() <= 1) {
        return;
    }

    if (_terminalDisplayHistoryIndex == -1) {
        _terminalDisplayHistoryIndex = reverse ? _terminalDisplayHistory.count() - 1 : 1;
    } else if (reverse) {
        if (_terminalDisplayHistoryIndex == 0) {
            _terminalDisplayHistoryIndex = _terminalDisplayHistory.count() - 1;
        } else {
            _terminalDisplayHistoryIndex--;
        }
    } else {
        if (_terminalDisplayHistoryIndex >= _terminalDisplayHistory.count() - 1) {
            _terminalDisplayHistoryIndex = 0;
        } else {
            _terminalDisplayHistoryIndex++;
        }
    }

    switchToTerminalDisplay(_terminalDisplayHistory[_terminalDisplayHistoryIndex]);
}

void ViewManager::lastUsedView()
{
    activateLastUsedView(false);
}

void ViewManager::lastUsedViewReverse()
{
    activateLastUsedView(true);
}

void ViewManager::toggleTwoViews()
{
    if (_terminalDisplayHistory.count() <= 1) {
        return;
    }

    switchToTerminalDisplay(_terminalDisplayHistory.at(1));
}

void ViewManager::detachActiveView()
{
    // find the currently active view and remove it from its container
    auto *container = activeContainer();
    if (container != nullptr && (container->findChildren<TerminalDisplay *>()).count() > 1) {
        auto activeSplitter = container->activeViewSplitter();
        activeSplitter->clearMaximized();
        auto terminal = activeSplitter->activeTerminalDisplay();
        auto newSplitter = new ViewSplitter();
        newSplitter->addTerminalDisplay(terminal, Qt::Horizontal);
        QHash<TerminalDisplay *, Session *> detachedSessions = forgetAll(newSplitter);
        Q_EMIT terminalsDetached(newSplitter, detachedSessions);
        focusAnotherTerminal(activeSplitter->getToplevelSplitter());
        toggleActionsBasedOnState();
    }
}

void ViewManager::detachActiveTab()
{
    auto *container = activeContainer();
    if (container == nullptr || container->count() < 2) {
        return;
    }
    const int currentIdx = container->currentIndex();
    detachTab(currentIdx);
}

void ViewManager::detachTab(int tabIdx)
{
    auto *container = qobject_cast<TabbedViewContainer *>(sender());
    if (container == nullptr) {
        container = activeContainer();
    }
    if (container == nullptr) {
        return;
    }

    ViewSplitter *splitter = container->viewSplitterAt(tabIdx);
    QHash<TerminalDisplay *, Session *> detachedSessions = forgetAll(container->viewSplitterAt(tabIdx));
    Q_EMIT terminalsDetached(splitter, detachedSessions);
}

void ViewManager::semanticSetupBash()
{
    int currentSessionId = currentSession();
    // At least one display/session exists if we are splitting
    Q_ASSERT(currentSessionId >= 0);

    Session *activeSession = SessionManager::instance()->idToSession(currentSessionId);
    Q_ASSERT(activeSession);

    activeSession->sendTextToTerminal(QStringLiteral(R"(if [[ ! $PS1 =~ 133 ]] ; then
        PS1='\[\e]133;L\a\]\[\e]133;D;$?\]\[\e]133;A\a\]'$PS1'\[\e]133;B\a\]' ;
        PS2='\[\e]133;A\a\]'$PS2'\[\e]133;B\a\]' ;
        PS0='\[\e]133;C\a\]' ; fi)"),
                                      QChar());
}

void ViewManager::toggleSemanticHints()
{
    int currentSessionId = currentSession();
    Q_ASSERT(currentSessionId >= 0);
    Session *activeSession = SessionManager::instance()->idToSession(currentSessionId);
    Q_ASSERT(activeSession);
    auto profile = SessionManager::instance()->sessionProfile(activeSession);

    profile->setProperty(Profile::SemanticHints, (profile->semanticHints() + 1) % 3);

    auto activeTerminalDisplay = activeContainer()->activeViewSplitter()->activeTerminalDisplay();
    const char *names[3] = {"Never", "Sometimes", "Always"};
    activeTerminalDisplay->showNotification(i18n("Semantic hints ") + i18n(names[profile->semanticHints()]));
    activeTerminalDisplay->update();
}

void ViewManager::toggleLineNumbers()
{
    int currentSessionId = currentSession();
    Q_ASSERT(currentSessionId >= 0);
    Session *activeSession = SessionManager::instance()->idToSession(currentSessionId);
    Q_ASSERT(activeSession);
    auto profile = SessionManager::instance()->sessionProfile(activeSession);

    profile->setProperty(Profile::LineNumbers, (profile->lineNumbers() + 1) % 3);

    auto activeTerminalDisplay = activeContainer()->activeViewSplitter()->activeTerminalDisplay();
    const char *names[3] = {"Never", "Sometimes", "Always"};
    activeTerminalDisplay->showNotification(i18n("Line numbers ") + i18n(names[profile->lineNumbers()]));
    activeTerminalDisplay->update();
}

QHash<TerminalDisplay *, Session *> ViewManager::forgetAll(ViewSplitter *splitter)
{
    splitter->setParent(nullptr);
    QHash<TerminalDisplay *, Session *> detachedSessions;
    const QList<TerminalDisplay *> displays = splitter->findChildren<TerminalDisplay *>();
    for (TerminalDisplay *terminal : displays) {
        Session *session = forgetTerminal(terminal);
        detachedSessions[terminal] = session;
    }
    return detachedSessions;
}

Session *ViewManager::forgetTerminal(TerminalDisplay *terminal)
{
    unregisterTerminal(terminal);

    removeController(terminal->sessionController());
    auto session = _sessionMap.take(terminal);
    if (session != nullptr) {
        disconnect(session, &Konsole::Session::finished, this, &Konsole::ViewManager::sessionFinished);
    }
    if (auto *container = containerForTerminal(terminal)) {
        container->disconnectTerminalDisplay(terminal);
    } else if (auto *container = activeContainer()) {
        container->disconnectTerminalDisplay(terminal);
    }
    updateTerminalDisplayHistory(terminal, true);
    return session;
}

void ViewManager::setContextMenuAdditionalActions(const QList<QAction *> &extension)
{
    contextMenuAdditionalActions = extension;
    Q_EMIT contextMenuAdditionalActionsChanged(extension);
}

Session *ViewManager::createSession(const Profile::Ptr &profile, const QString &directory)
{
    Session *session = SessionManager::instance()->createSession(profile);
    Q_ASSERT(session);
    if (!directory.isEmpty()) {
        session->setInitialWorkingDirectory(directory);
    }
    session->addEnvironmentEntry(QStringLiteral("KMUX_DBUS_WINDOW=/Windows/%1").arg(managerId()));

    // Determine container context for the new session.
    // Priority: inherit from active session (if enabled)
    if (profile && profile->inheritContainerContext() && _pluggedController) {
        Session *activeSession = _pluggedController->session();
        if (activeSession && activeSession->isInContainer()) {
            session->setContainerContext(activeSession->containerContext());
        }
    }

    updateAutoContainerTabColor(session);

    return session;
}

void ViewManager::sessionFinished(Session *session)
{
    // if this slot is called after the view manager's main widget
    // has been destroyed, do nothing
    if (_workspaceContainer.isNull()) {
        return;
    }

    if (_navigationMethod == TabbedNavigation) {
        // The last session/view in the whole window, emit empty()
        // so that close() is called in MainWindow, fixes #432077
        if (_sessionMap.size() == 1) {
            Q_EMIT empty();
            return;
        }
    }

    Q_ASSERT(session);

    auto view = _sessionMap.key(session);
    _sessionMap.remove(view);

    if (SessionManager::instance()->isClosingAllSessions()) {
        return;
    }

    // Before deleting the view, let's unmaximize if it's maximized.
    auto *splitter = ViewSplitter::parentSplitterForDisplay(view);
    if (splitter == nullptr) {
        return;
    }
    splitter->clearMaximized();

    view->deleteLater();
    connect(view, &QObject::destroyed, this, [this]() {
        toggleActionsBasedOnState();
    });

    // Only remove the controller from factory() if it's actually controlling
    // the session from the sender.
    // This fixes BUG: 348478 - messed up menus after a detached tab is closed
    if ((!_pluggedController.isNull()) && (_pluggedController->session() == session)) {
        // This is needed to remove this controller from factory() in
        // order to prevent BUG: 185466 - disappearing menu popup
        Q_EMIT unplugController(_pluggedController);
    }

    if (!_sessionMap.empty() && containerForTerminal(view) == activeContainer()) {
        updateTerminalDisplayHistory(view, true);
        focusAnotherTerminal(splitter->getToplevelSplitter());
    }
}

void ViewManager::focusAnotherTerminal(ViewSplitter *toplevelSplitter)
{
    auto tabTterminalDisplays = toplevelSplitter->findChildren<TerminalDisplay *>();
    if (tabTterminalDisplays.count() == 0) {
        return;
    }

    if (tabTterminalDisplays.count() > 1) {
        // Give focus to the last used terminal in this tab
        for (const auto *historyItem : std::as_const(_terminalDisplayHistory)) {
            for (auto *terminalDisplay : std::as_const(tabTterminalDisplays)) {
                if (terminalDisplay == historyItem) {
                    terminalDisplay->setFocus(Qt::OtherFocusReason);
                    return;
                }
            }
        }
    }

    if (_terminalDisplayHistory.count() >= 1) {
        // Give focus to the last used terminal tab
        switchToTerminalDisplay(_terminalDisplayHistory[0]);
    }
}

void ViewManager::activateView(TerminalDisplay *view)
{
    if (view) {
        // focus the activated view, this will cause the SessionController
        // to notify the world that the view has been focused and the appropriate UI
        // actions will be plugged in.
        view->setFocus(Qt::OtherFocusReason);
    }
}

void ViewManager::splitLeftRight()
{
    splitView(Qt::Horizontal);
}

void ViewManager::splitTopBottom()
{
    splitView(Qt::Vertical);
}

void ViewManager::splitAuto(bool fromNextTab)
{
    Qt::Orientation orientation;
    auto activeTerminalDisplay = activeContainer()->activeViewSplitter()->activeTerminalDisplay();
    if (activeTerminalDisplay->width() > activeTerminalDisplay->height()) {
        orientation = Qt::Horizontal;
    } else {
        orientation = Qt::Vertical;
    }
    splitView(orientation, fromNextTab);
}

void ViewManager::splitLeftRightNextTab()
{
    splitView(Qt::Horizontal, true);
}

void ViewManager::splitTopBottomNextTab()
{
    splitView(Qt::Vertical, true);
}

void ViewManager::splitAutoNextTab()
{
    splitAuto(true);
}

void ViewManager::splitView(Qt::Orientation orientation, bool fromNextTab)
{
    TerminalDisplay *terminalDisplay;
    TerminalDisplay *focused;
    auto *container = activeContainer();
    if (container == nullptr) {
        return;
    }

    if (fromNextTab) {
        int tabId = container->indexOf(container->activeViewSplitter());
        auto nextTab = container->viewSplitterAt(tabId + 1);

        if (!nextTab) {
            return;
        }
        terminalDisplay = nextTab->activeTerminalDisplay();
        focused = container->activeViewSplitter()->activeTerminalDisplay();
    } else {
        int currentSessionId = currentSession();
        // At least one display/session exists if we are splitting
        Q_ASSERT(currentSessionId >= 0);

        Session *activeSession = SessionManager::instance()->idToSession(currentSessionId);
        Q_ASSERT(activeSession);

        auto profile = SessionManager::instance()->sessionProfile(activeSession);

        const QString directory = profile->startInCurrentSessionDir() ? activeSession->currentWorkingDirectory() : QString();
        auto *session = createSession(profile, directory);

        focused = terminalDisplay = createView(session);
        Q_EMIT activeViewChanged(activeViewController());
    }

    container->splitView(terminalDisplay, orientation);

    toggleActionsBasedOnState();

    // focus the new container if created, else keep the currently focused view
    focused->setFocus();
}

void ViewManager::expandActiveContainer()
{
    activeContainer()->activeViewSplitter()->adjustActiveTerminalDisplaySize(10);
}

void ViewManager::shrinkActiveContainer()
{
    activeContainer()->activeViewSplitter()->adjustActiveTerminalDisplaySize(-10);
}

void ViewManager::equalSizeAllContainers()
{
    std::function<void(ViewSplitter *)> processChildren = [&processChildren](ViewSplitter *viewSplitter) -> void {
        // divide the size of the parent widget by the amount of children splits
        auto hintSize = viewSplitter->size();
        auto sizes = viewSplitter->sizes();
        auto sharedSize = hintSize / sizes.size();
        if (viewSplitter->orientation() == Qt::Horizontal) {
            for (auto &&size : sizes) {
                size = sharedSize.width();
            }
        } else {
            for (auto &&size : sizes) {
                size = sharedSize.height();
            }
        }
        // set new sizes
        viewSplitter->setSizes(sizes);

        // set equal sizes for each splitter children
        for (auto &&child : viewSplitter->children()) {
            auto childViewSplitter = qobject_cast<ViewSplitter *>(child);
            if (childViewSplitter) {
                processChildren(childViewSplitter);
            }
        }
    };
    processChildren(activeContainer()->activeViewSplitter()->getToplevelSplitter());
}

SessionController *ViewManager::createController(Session *session, TerminalDisplay *view)
{
    // create a new controller for the session, and ensure that this view manager
    // is notified when the view gains the focus
    auto controller = new SessionController(session, view, this);
    connect(controller, &Konsole::SessionController::viewFocused, this, &Konsole::ViewManager::controllerChanged);
    connect(session, &Konsole::Session::destroyed, controller, &Konsole::SessionController::deleteLater);
    connect(session, &Konsole::Session::primaryScreenInUse, controller, &Konsole::SessionController::setupPrimaryScreenSpecificActions);
    connect(session, &Konsole::Session::selectionChanged, controller, &Konsole::SessionController::selectionChanged);
    connect(session, &Konsole::Session::containerContextChanged, this, &Konsole::ViewManager::handleSessionContainerContextChanged, Qt::UniqueConnection);
    connect(view, &Konsole::TerminalDisplay::destroyed, controller, &Konsole::SessionController::deleteLater);
    connect(controller, &Konsole::SessionController::viewDragAndDropped, this, &Konsole::ViewManager::forgetController);
    connect(controller, &Konsole::SessionController::requestSplitViewLeftRight, this, &Konsole::ViewManager::splitLeftRight);
    connect(controller, &Konsole::SessionController::requestSplitViewTopBottom, this, &Konsole::ViewManager::splitTopBottom);
    connect(this, &Konsole::ViewManager::contextMenuAdditionalActionsChanged, controller, &Konsole::SessionController::setContextMenuAdditionalActions);
    connect(controller, &Konsole::SessionController::titleChanged, this, [this, view](ViewProperties *) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(controller, &Konsole::SessionController::iconChanged, this, [this, view](ViewProperties *) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(controller, &Konsole::SessionController::activity, this, [this, view](ViewProperties *) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(controller, &Konsole::SessionController::notificationChanged, this, [this, view](ViewProperties *, Session::Notification, bool) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(controller, &Konsole::SessionController::currentDirectoryChanged, this, [this, view](const QString &) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(session, &Konsole::Session::started, this, [this, view] {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(session, &Konsole::Session::notificationsChanged, this, [this, view](Session::Notification, bool) {
        refreshProjectSummary(containerForTerminal(view));
    });
    connect(session, &Konsole::Session::terminalNotificationReceived, this, [this, session, view](const QString &title, const QString &body) {
        auto *container = containerForTerminal(view);
        if (container != nullptr && !_workspaceContainer.isNull()) {
            QString notification = body.simplified();
            if (!title.simplified().isEmpty() && !body.simplified().isEmpty()) {
                notification = i18nc("@info:project notification title and body", "%1: %2", title.simplified(), body.simplified());
            } else if (!title.simplified().isEmpty()) {
                notification = title.simplified();
            }
            if (!notification.isEmpty()) {
                _workspaceContainer->setProjectNotification(container, notification);
            }
        }
        markSessionAttention(session, container);
        refreshProjectSummary(container);
    });
    connect(session,
            &Konsole::Session::projectStatusChanged,
            this,
            [this, session, view](const QString &status, qlonglong agentProcessId, const QString &agent, const QString &event) {
                auto *container = containerForTerminal(view);
                setSessionProjectStatus(session, container, status, agentProcessId, agent, event);
                refreshProjectSummary(container);
            });
    connect(view, &Konsole::TerminalDisplay::keyPressedSignal, this, [this, session, view](QKeyEvent *keyEvent) {
        handleSessionTerminalDecisionKey(session, containerForTerminal(view), keyEvent);
    });
    connect(session, &QObject::destroyed, this, [this, session] {
        _sessionsNeedingAttention.remove(session);
        _sessionProjectStatuses.remove(session);
        updateProjectStatusProcessTimer();
    });

    // if this is the first controller created then set it as the active controller
    if (_pluggedController.isNull()) {
        controllerChanged(controller);
    }

    if (!contextMenuAdditionalActions.isEmpty()) {
        controller->setContextMenuAdditionalActions(contextMenuAdditionalActions);
    }

    refreshProjectSummary(containerForTerminal(view));
    return controller;
}

void ViewManager::handleSessionContainerContextChanged(const ContainerInfo &container)
{
    Q_UNUSED(container)
    auto *session = qobject_cast<Session *>(sender());
    if (session == nullptr) {
        return;
    }

    if (session->isInContainer()) {
        ContainerSessionState::clearPendingContainerInfo(session);
    }

    updateAutoContainerTabColor(session);
}

void ViewManager::updateAutoContainerTabColor(Session *session)
{
    if (session == nullptr || session->isTabColorSetByUser()) {
        return;
    }

    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    if (!profile || profile->tabColor().isValid()) {
        return;
    }

    if (!session->isInContainer()) {
        const auto pending = ContainerSessionState::pendingContainerInfo(session);
        if (pending.isActive()) {
            session->setColor(colorForContainerKey(pending.key));
            return;
        }
        session->setColor(QColor());
        return;
    }

    const QString key = ContainerRegistry::keyFromContainerInfo(session->containerContext());
    if (!key.isEmpty()) {
        session->setColor(colorForContainerKey(key));
    }
}

QColor ViewManager::colorForContainerKey(const QString &containerKey)
{
    return ContainerSessionState::colorForContainerKey(containerKey);
}

void ViewManager::forgetController(SessionController *controller)
{
    Q_ASSERT(controller->session() != nullptr && controller->view() != nullptr);

    forgetTerminal(controller->view());
    toggleActionsBasedOnState();
}

// should this be handed by ViewManager::unplugController signal
void ViewManager::removeController(SessionController *controller)
{
    Q_EMIT unplugController(controller);

    if (_pluggedController == controller) {
        _pluggedController.clear();
    }
    // disconnect now!! important as a focus change may happen in between and we will end up using a deleted controller
    disconnect(controller, &Konsole::SessionController::viewFocused, this, &Konsole::ViewManager::controllerChanged);
    controller->deleteLater();
}

void ViewManager::controllerChanged(SessionController *controller)
{
    if (controller == _pluggedController) {
        return;
    }

    if (auto *container = containerForTerminal(controller->view())) {
        _workspaceContainer->activateProject(container);
        container->setFocusProxy(controller->view());
    }
    updateTerminalDisplayHistory(controller->view());

    _pluggedController = controller;
    Q_EMIT activeViewChanged(controller);
}

SessionController *ViewManager::activeViewController() const
{
    return _pluggedController;
}

void ViewManager::attachView(TerminalDisplay *terminal, Session *session)
{
    connect(session, &Konsole::Session::finished, this, &Konsole::ViewManager::sessionFinished, Qt::UniqueConnection);

    // Disconnect from the other viewcontainer.
    unregisterTerminal(terminal);

    // reconnect on this container.
    registerTerminal(terminal);

    _sessionMap[terminal] = session;
    createController(session, terminal);
    toggleActionsBasedOnState();
    _terminalDisplayHistory.append(terminal);
}

TerminalDisplay *ViewManager::findTerminalDisplay(int viewId)
{
    for (auto i = _sessionMap.keyBegin(); i != _sessionMap.keyEnd(); ++i) {
        TerminalDisplay *view = *i;
        if (view->id() == viewId)
            return view;
    }

    return nullptr;
}

void ViewManager::setCurrentView(TerminalDisplay *view)
{
    auto *container = containerForTerminal(view);
    if (container == nullptr) {
        container = activeContainer();
    }
    if (container == nullptr) {
        return;
    }

    _workspaceContainer->activateProject(container);
    auto parentSplitter = ViewSplitter::parentSplitterForDisplay(view);
    container->setCurrentWidget(parentSplitter->getToplevelSplitter());
    view->setFocus();
    setCurrentSession(_sessionMap[view]->sessionId());
}

TerminalDisplay *ViewManager::createView(Session *session)
{
    // notify this view manager when the session finishes so that its view
    // can be deleted
    //
    // Use Qt::UniqueConnection to avoid duplicate connection
    connect(session, &Konsole::Session::finished, this, &Konsole::ViewManager::sessionFinished, Qt::UniqueConnection);
    TerminalDisplay *display = createTerminalDisplay();
    createController(session, display);

    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);
    applyProfileToView(display, profile);

    // set initial size
    const QSize &preferredSize = session->preferredSize();

    display->setSize(preferredSize.width(), preferredSize.height());

    _sessionMap[display] = session;
    session->addView(display);
    _terminalDisplayHistory.append(display);

    // tell the session whether it has a light or dark background
    session->setDarkBackground(colorSchemeForProfile(profile)->hasDarkBackground());
    display->setFocus(Qt::OtherFocusReason);
    //     updateDetachViewState();
    connect(display, &TerminalDisplay::activationRequest, this, &Konsole::ViewManager::activationRequest);

    return display;
}

TabbedViewContainer *ViewManager::createContainer()
{
    auto *container = new TabbedViewContainer(this, nullptr);
    container->setNavigationVisibility(_navigationVisibility);
    connect(container, &TabbedViewContainer::detachTab, this, &ViewManager::detachTab);
    connect(container, &TabbedViewContainer::empty, this, &ViewManager::containerEmptied);
    connect(container, &TabbedViewContainer::tabContextMenuAboutToShow, this, [this, container](QMenu *menu, int tabIndex) {
        addMoveTabToProjectMenu(menu, container, tabIndex);
    });

    // connect signals and slots
    connect(container, &Konsole::TabbedViewContainer::viewAdded, this, [this, container]() {
        containerViewsChanged(container);
    });
    connect(container, &Konsole::TabbedViewContainer::viewRemoved, this, [this, container]() {
        containerViewsChanged(container);
    });

    connect(container, &TabbedViewContainer::newViewRequest, this, &ViewManager::newViewRequest);
    connect(container, &Konsole::TabbedViewContainer::newViewWithProfileRequest, this, &Konsole::ViewManager::newViewWithProfileRequest);
    connect(container, &Konsole::TabbedViewContainer::newViewInContainerRequest, this, &Konsole::ViewManager::newViewInContainerRequest);
    connect(container, &Konsole::TabbedViewContainer::activeViewChanged, this, &Konsole::ViewManager::activateView);
    connect(container, &TabbedViewContainer::viewAdded, this, &ViewManager::toggleActionsBasedOnState);
    connect(container, &QTabWidget::currentChanged, this, &ViewManager::toggleActionsBasedOnState);
    connect(container, &QTabWidget::currentChanged, this, [this, container]() {
        refreshProjectSummary(container);
    });
    connect(container, &Konsole::TabbedViewContainer::activeViewChanged, this, [this, container](TerminalDisplay *) {
        refreshProjectSummary(container);
    });
    connect(container, &TabbedViewContainer::viewRemoved, this, &ViewManager::toggleActionsBasedOnState);

    return container;
}

void ViewManager::createProject()
{
    if (_navigationMethod == NoNavigation) {
        return;
    }

    auto *container = createContainer();
    _workspaceContainer->addProject(container, _workspaceContainer->nextDefaultProjectTitle());
    Q_EMIT newViewRequest();
}

void ViewManager::closeProject(TabbedViewContainer *container)
{
    if (container == nullptr || _workspaceContainer->projectCount() <= 1) {
        return;
    }

    const int tabCount = container->count();
    if (tabCount == 0) {
        containerEmptied(container);
        return;
    }

    if (!confirmCloseProject(container)) {
        return;
    }

    const auto controllers = sessionControllersForContainer(container);
    for (SessionController *controller : controllers) {
        if (controller == nullptr || controller->session() == nullptr) {
            continue;
        }

        if (!controller->session()->closeInNormalWay()) {
            if (controller->confirmForceClose()) {
                controller->session()->closeInForceWay();
            }
        }
    }
}

QList<SessionController *> ViewManager::sessionControllersForContainer(TabbedViewContainer *container) const
{
    QList<SessionController *> controllers;
    QSet<Session *> sessions;

    for (int i = 0; container != nullptr && i < container->count(); ++i) {
        auto *splitter = container->viewSplitterAt(i);
        if (splitter == nullptr) {
            continue;
        }

        const auto terminals = splitter->findChildren<TerminalDisplay *>();
        for (TerminalDisplay *terminal : terminals) {
            auto *controller = terminal->sessionController();
            if (controller == nullptr || controller->session() == nullptr || sessions.contains(controller->session())) {
                continue;
            }

            sessions.insert(controller->session());
            controllers.append(controller);
        }
    }

    return controllers;
}

bool ViewManager::confirmCloseProject(TabbedViewContainer *container) const
{
    QStringList processesRunning;
    const auto controllers = sessionControllersForContainer(container);

    for (SessionController *controller : controllers) {
        Session *session = controller->session();
        if ((session == nullptr) || !session->isForegroundProcessActive()) {
            continue;
        }

        const QString defaultProc = session->program().split(QLatin1Char('/')).last();
        const QString currentProc = session->foregroundProcessName().split(QLatin1Char('/')).last();

        if (currentProc.isEmpty()) {
            continue;
        }

        if (defaultProc != currentProc) {
            processesRunning.append(currentProc);
        }
    }

    const int openTerminals = controllers.count();
    if (processesRunning.isEmpty() && openTerminals < 2) {
        return true;
    }

    int result = KMessageBox::Cancel;
    if (!processesRunning.isEmpty()) {
        result = KMessageBox::warningTwoActionsList(_workspaceContainer,
                                                    i18ncp("@info",
                                                           "There is a process running in this project. "
                                                           "Do you still want to close it?",
                                                           "There are %1 processes running in this project. "
                                                           "Do you still want to close it?",
                                                           processesRunning.count()),
                                                    processesRunning,
                                                    i18nc("@title", "Confirm Close"),
                                                    KGuiItem(i18nc("@action:button", "Close &Project"), QStringLiteral("window-close")),
                                                    KStandardGuiItem::cancel(),
                                                    QStringLiteral("CloseProjectWorkspaceWithProcesses"));
    } else {
        result = KMessageBox::warningTwoActions(_workspaceContainer,
                                                i18ncp("@info",
                                                       "There is %1 open terminal in this project. "
                                                       "Do you still want to close it?",
                                                       "There are %1 open terminals in this project. "
                                                       "Do you still want to close it?",
                                                       openTerminals),
                                                i18nc("@title", "Confirm Close"),
                                                KGuiItem(i18nc("@action:button", "Close &Project"), QStringLiteral("window-close")),
                                                KStandardGuiItem::cancel(),
                                                QStringLiteral("CloseProjectWorkspaceWithTabs"));
    }

    return result == KMessageBox::PrimaryAction;
}

void ViewManager::activeProjectChanged(TabbedViewContainer *container)
{
    if (container == nullptr || container == _viewContainer) {
        return;
    }

    _viewContainer = container;

    if (auto *splitter = container->activeViewSplitter()) {
        if (auto *terminal = splitter->activeTerminalDisplay()) {
            terminal->setFocus(Qt::OtherFocusReason);
        }
    }

    toggleActionsBasedOnState();
    clearProjectAttention(container);
    refreshProjectSummary(container);
    Q_EMIT viewPropertiesChanged(viewProperties());
}

void ViewManager::containerEmptied(TabbedViewContainer *container)
{
    if (container == nullptr || _workspaceContainer.isNull()) {
        return;
    }

    if (_workspaceContainer->projectCount() <= 1) {
        Q_EMIT empty();
        return;
    }

    _workspaceContainer->removeProject(container);
    if (container == _viewContainer) {
        _viewContainer = _workspaceContainer->activeContainer();
    }
    container->deleteLater();
}

void ViewManager::setNavigationMethod(NavigationMethod method)
{
    Q_ASSERT(_actionCollection);
    if (_actionCollection == nullptr) {
        return;
    }
    KActionCollection *collection = _actionCollection;

    _navigationMethod = method;

    // FIXME: The following disables certain actions for the KPart that it
    // doesn't actually have a use for, to avoid polluting the action/shortcut
    // namespace of an application using the KPart (otherwise, a shortcut may
    // be in use twice, and the user gets to see an "ambiguous shortcut over-
    // load" error dialog). However, this approach sucks - it's the inverse of
    // what it should be. Rather than disabling actions not used by the KPart,
    // a method should be devised to only enable those that are used, perhaps
    // by using a separate action collection.

    const bool enable = (method != NoNavigation);
    if (!_workspaceContainer.isNull()) {
        _workspaceContainer->setProjectNavigationVisible(enable);
    }

    auto enableAction = [&enable, &collection](const QString &actionName) {
        auto *action = collection->action(actionName);
        if (action != nullptr) {
            action->setEnabled(enable);
        }
    };

    enableAction(QStringLiteral("next-view"));
    enableAction(QStringLiteral("previous-view"));
    enableAction(QStringLiteral("last-tab"));
    enableAction(QStringLiteral("last-used-tab"));
    enableAction(QStringLiteral("last-used-tab-reverse"));
    enableAction(QStringLiteral("split-view-left-right"));
    enableAction(QStringLiteral("split-view-top-bottom"));
    enableAction(QStringLiteral("split-view-left-right-next-tab"));
    enableAction(QStringLiteral("split-view-top-bottom-next-tab"));
    enableAction(QStringLiteral("rename-session"));
    enableAction(QStringLiteral("move-view-left"));
    enableAction(QStringLiteral("move-view-right"));
    enableAction(QStringLiteral("add-workspace"));

    toggleActionsBasedOnState();
}

ViewManager::NavigationMethod ViewManager::navigationMethod() const
{
    return _navigationMethod;
}

void ViewManager::containerViewsChanged(TabbedViewContainer *container)
{
    refreshProjectSummary(container);
    // TODO: Verify that this is right.
    Q_EMIT viewPropertiesChanged(viewProperties());
}

void ViewManager::viewDestroyed(QWidget *view)
{
    // Note: the received QWidget has already been destroyed, so
    // using dynamic_cast<> or qobject_cast<> does not work here
    // We only need the pointer address to look it up below
    auto *display = reinterpret_cast<TerminalDisplay *>(view);

    // 1. detach view from session
    // 2. if the session has no views left, close it
    Session *session = _sessionMap[display];
    _sessionMap.remove(display);
    if (session != nullptr) {
        if (session->views().count() == 0) {
            session->close();
        }
    }

    // we only update the focus if the splitter is still alive
    toggleActionsBasedOnState();

    // The below causes the menus  to be messed up
    // Only happens when using the tab bar close button
    //    if (_pluggedController)
    //        Q_EMIT unplugController(_pluggedController);
}

TerminalDisplay *ViewManager::createTerminalDisplay()
{
    auto display = new TerminalDisplay(nullptr);
    registerTerminal(display);

    return display;
}

std::shared_ptr<const ColorScheme> ViewManager::colorSchemeForProfile(const Profile::Ptr &profile)
{
    std::shared_ptr<const ColorScheme> colorScheme = ColorSchemeManager::instance()->findColorScheme(profile->colorScheme());
    if (colorScheme == nullptr) {
        colorScheme = ColorSchemeManager::instance()->defaultColorScheme();
    }
    Q_ASSERT(colorScheme);

    return colorScheme;
}

bool ViewManager::profileHasBlurEnabled(const Profile::Ptr &profile)
{
    return colorSchemeForProfile(profile)->blur();
}

void ViewManager::applyProfileToView(TerminalDisplay *view, const Profile::Ptr &profile)
{
    Q_ASSERT(profile);
    view->applyProfile(profile);
    Q_EMIT updateWindowIcon();
    Q_EMIT blurSettingChanged(view->colorScheme()->blur());
}

void ViewManager::updateViewsForSession(Session *session)
{
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(session);

    const QList<TerminalDisplay *> sessionMapKeys = _sessionMap.keys(session);
    for (TerminalDisplay *view : sessionMapKeys) {
        applyProfileToView(view, profile);
    }
}

void ViewManager::profileChanged(const Profile::Ptr &profile)
{
    // update all views associated with this profile
    QHashIterator<TerminalDisplay *, Session *> iter(_sessionMap);
    while (iter.hasNext()) {
        iter.next();

        // if session uses this profile, update the display
        if (iter.key() != nullptr && iter.value() != nullptr && SessionManager::instance()->sessionProfile(iter.value()) == profile) {
            applyProfileToView(iter.key(), profile);
        }
    }
}

QList<ViewProperties *> ViewManager::viewProperties() const
{
    QList<ViewProperties *> list;

    TabbedViewContainer *container = _workspaceContainer != nullptr ? _workspaceContainer->activeContainer() : nullptr;
    if (container == nullptr) {
        return {};
    }

    const auto terminalContainers = container->findChildren<TerminalDisplay *>();
    list.reserve(terminalContainers.size());

    for (auto terminalDisplay : terminalContainers) {
        list.append(terminalDisplay->sessionController());
    }

    return list;
}

namespace
{
QJsonObject saveSessionTerminal(TerminalDisplay *terminalDisplay)
{
    QJsonObject thisTerminal;
    auto terminalSession = terminalDisplay->sessionController()->session();
    const Profile::Ptr profile = SessionManager::instance()->sessionProfile(terminalSession);
    const int sessionRestoreId = SessionManager::instance()->getRestoreId(terminalSession);
    thisTerminal.insert(QStringLiteral("SessionRestoreId"), sessionRestoreId);
    thisTerminal.insert(QStringLiteral("Columns"), terminalDisplay->columns());
    thisTerminal.insert(QStringLiteral("Lines"), terminalDisplay->lines());
    thisTerminal.insert(QStringLiteral("WorkingDirectory"), terminalDisplay->session()->currentWorkingDirectory());
    thisTerminal.insert(QStringLiteral("ProfilePath"), profile ? profile->path() : QString());
    thisTerminal.insert(QStringLiteral("ProfileName"), profile ? profile->name() : QString());
    thisTerminal.insert(QStringLiteral("Command"), terminalSession->program());
    thisTerminal.insert(QStringLiteral("Arguments"), QJsonArray::fromStringList(terminalSession->arguments()));
    thisTerminal.insert(QStringLiteral("Environment"), QJsonArray::fromStringList(profile ? profile->environment() : QStringList()));
    thisTerminal.insert(QStringLiteral("AutoClose"), terminalSession->autoClose());
    thisTerminal.insert(QStringLiteral("LocalTabTitleFormat"), terminalSession->tabTitleFormat(Session::LocalTabTitle));
    thisTerminal.insert(QStringLiteral("RemoteTabTitleFormat"), terminalSession->tabTitleFormat(Session::RemoteTabTitle));
    thisTerminal.insert(QStringLiteral("TabColor"), terminalSession->color().isValid() ? terminalSession->color().name(QColor::HexArgb) : QString());
    thisTerminal.insert(QStringLiteral("TabActivityColor"),
                        terminalSession->activityColor().isValid() ? terminalSession->activityColor().name(QColor::HexArgb) : QString());
    thisTerminal.insert(QStringLiteral("Encoding"), QString::fromUtf8(terminalSession->codec()));
    thisTerminal.insert(QStringLiteral("BadgeEnabled"), terminalSession->badgeEnabled());
    thisTerminal.insert(QStringLiteral("BadgeText"), terminalSession->badgeText());
    thisTerminal.insert(QStringLiteral("BadgeFontFamily"), terminalSession->badgeFontFamily());
    thisTerminal.insert(QStringLiteral("BadgeFontSize"), terminalSession->badgeFontSize());
    thisTerminal.insert(QStringLiteral("BadgeColor"),
                        terminalSession->badgeColor().isValid() ? terminalSession->badgeColor().name(QColor::HexArgb) : QString());
    thisTerminal.insert(QStringLiteral("BadgeTextOnly"), terminalSession->badgeTextOnly());
    thisTerminal.insert(QStringLiteral("BadgeTransparency"), terminalSession->badgeTransparency());
    return thisTerminal;
}

QJsonObject saveSessionsRecurse(QSplitter *splitter)
{
    QJsonObject thisSplitter;
    thisSplitter.insert(QStringLiteral("Orientation"), splitter->orientation() == Qt::Horizontal ? QStringLiteral("Horizontal") : QStringLiteral("Vertical"));

    QJsonArray internalWidgets;
    for (int i = 0; i < splitter->count(); i++) {
        auto *widget = splitter->widget(i);
        auto *maybeSplitter = qobject_cast<QSplitter *>(widget);
        auto *maybeTerminalDisplay = ViewSplitter::terminalDisplayForWidget(widget);

        if (maybeSplitter != nullptr) {
            internalWidgets.append(saveSessionsRecurse(maybeSplitter));
        } else if (maybeTerminalDisplay != nullptr) {
            internalWidgets.append(saveSessionTerminal(maybeTerminalDisplay));
        }
    }
    thisSplitter.insert(QStringLiteral("Widgets"), internalWidgets);
    return thisSplitter;
}

QJsonArray saveContainerSessions(TabbedViewContainer *container)
{
    QJsonArray rootArray;
    for (int i = 0; container != nullptr && i < container->count(); i++) {
        auto *splitter = qobject_cast<QSplitter *>(container->widget(i));
        if (splitter != nullptr) {
            rootArray.append(saveSessionsRecurse(splitter));
        }
    }

    return rootArray;
}

} // namespace

void ViewManager::saveLayoutFile()
{
    saveLayout(QFileDialog::getSaveFileName(this->widget(),
                                            i18nc("@title:window", "Save Tab Layout"),
                                            QStringLiteral("~/"),
                                            i18nc("@item:inlistbox", "Konsole View Layout (*.json)")));
}

void ViewManager::saveLayout(QString fileName)
{
    // User pressed cancel in dialog
    if (fileName.isEmpty()) {
        return;
    }

    if (!fileName.endsWith(QStringLiteral(".json"))) {
        fileName.append(QStringLiteral(".json"));
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        KMessageBox::error(this->widget(), i18nc("@label:textbox", "A problem occurred when saving the Layout.\n%1", file.fileName()));
    }

    QJsonObject jsonSplit = saveSessionsRecurse(activeContainer()->activeViewSplitter());

    if (!jsonSplit.isEmpty()) {
        file.write(QJsonDocument(jsonSplit).toJson());
        qDebug() << "Maybe was saved";
    }
}

void ViewManager::saveSessions(KConfigGroup &group)
{
    auto *container = activeContainer();
    QJsonArray rootArray = saveContainerSessions(container);

    group.writeEntry("Tabs", QJsonDocument(rootArray).toJson(QJsonDocument::Compact));
    group.writeEntry("Active", container != nullptr ? container->currentIndex() : 0);
    if (!_workspaceContainer.isNull()) {
        group.writeEntry("ProjectRailWidth", _workspaceContainer->projectRailWidth());
    }

    QJsonArray projectArray;
    int activeProjectIndex = 0;
    if (!_workspaceContainer.isNull()) {
        const QList<TabbedViewContainer *> containers = _workspaceContainer->containers();
        for (int i = 0; i < containers.count(); ++i) {
            auto *projectContainer = containers.at(i);
            if (projectContainer == container) {
                activeProjectIndex = i;
            }

            QJsonObject project;
            project.insert(QStringLiteral("Title"), _workspaceContainer->projectTitle(projectContainer));
            project.insert(QStringLiteral("Tabs"), saveContainerSessions(projectContainer));
            project.insert(QStringLiteral("Active"), projectContainer != nullptr ? projectContainer->currentIndex() : 0);
            projectArray.append(project);
        }
    }

    group.writeEntry("Projects", QJsonDocument(projectArray).toJson(QJsonDocument::Compact));
    group.writeEntry("ActiveProject", activeProjectIndex);
}

namespace
{
QStringList jsonStringList(const QJsonValue &value)
{
    QStringList result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &item : array) {
        result.append(item.toString());
    }
    return result;
}

Profile::Ptr savedSessionProfile(const QJsonObject &sessionObject)
{
    Profile::Ptr profile;
    const QString profilePath = sessionObject[QStringLiteral("ProfilePath")].toString();
    if (!profilePath.isEmpty()) {
        profile = ProfileManager::instance()->loadProfile(profilePath);
    }

    const QString profileName = sessionObject[QStringLiteral("ProfileName")].toString();
    if (!profile && !profileName.isEmpty()) {
        const QList<Profile::Ptr> profiles = ProfileManager::instance()->allProfiles();
        const auto profileIterator = std::find_if(profiles.cbegin(), profiles.cend(), [&profileName](const Profile::Ptr &candidate) {
            return candidate->name() == profileName;
        });
        if (profileIterator != profiles.cend()) {
            profile = *profileIterator;
        }
    }

    if (!profile) {
        profile = ProfileManager::instance()->defaultProfile();
    }

    const bool hasRuntimeSettings = sessionObject.contains(QStringLiteral("Command")) || sessionObject.contains(QStringLiteral("Arguments"))
        || sessionObject.contains(QStringLiteral("Environment"));
    if (!hasRuntimeSettings) {
        return profile;
    }

    Profile::Ptr restoredProfile(new Profile(profile));
    restoredProfile->setHidden(true);
    restoredProfile->setProperty(Profile::Name, profileName.isEmpty() ? profile->name() : profileName);
    restoredProfile->setProperty(Profile::Path, profilePath.isEmpty() ? profile->path() : profilePath);
    if (sessionObject.contains(QStringLiteral("Command"))) {
        restoredProfile->setProperty(Profile::Command, sessionObject[QStringLiteral("Command")].toString());
    }
    if (sessionObject.contains(QStringLiteral("Arguments"))) {
        restoredProfile->setProperty(Profile::Arguments, jsonStringList(sessionObject[QStringLiteral("Arguments")]));
    }
    if (sessionObject.contains(QStringLiteral("Environment"))) {
        restoredProfile->setProperty(Profile::Environment, jsonStringList(sessionObject[QStringLiteral("Environment")]));
    }
    return restoredProfile;
}

void restoreColdSessionState(Session *session, const QJsonObject &sessionObject)
{
    if (sessionObject.contains(QStringLiteral("AutoClose"))) {
        session->setAutoClose(sessionObject[QStringLiteral("AutoClose")].toBool());
    }
    if (sessionObject.contains(QStringLiteral("LocalTabTitleFormat"))) {
        session->setTabTitleFormat(Session::LocalTabTitle, sessionObject[QStringLiteral("LocalTabTitleFormat")].toString());
    }
    if (sessionObject.contains(QStringLiteral("RemoteTabTitleFormat"))) {
        session->setTabTitleFormat(Session::RemoteTabTitle, sessionObject[QStringLiteral("RemoteTabTitleFormat")].toString());
    }
    if (sessionObject.contains(QStringLiteral("TabColor"))) {
        session->setColor(QColor(sessionObject[QStringLiteral("TabColor")].toString()));
    }
    if (sessionObject.contains(QStringLiteral("TabActivityColor"))) {
        session->setActivityColor(QColor(sessionObject[QStringLiteral("TabActivityColor")].toString()));
    }
    if (sessionObject.contains(QStringLiteral("Encoding"))) {
        session->setCodec(sessionObject[QStringLiteral("Encoding")].toString().toUtf8());
    }
    if (sessionObject.contains(QStringLiteral("BadgeEnabled"))) {
        session->setBadgeEnabled(sessionObject[QStringLiteral("BadgeEnabled")].toBool());
        session->setBadgeText(sessionObject[QStringLiteral("BadgeText")].toString());
        session->setBadgeFontFamily(sessionObject[QStringLiteral("BadgeFontFamily")].toString());
        session->setBadgeFontSize(sessionObject[QStringLiteral("BadgeFontSize")].toInt());
        session->setBadgeColor(QColor(sessionObject[QStringLiteral("BadgeColor")].toString()));
        session->setBadgeTextOnly(sessionObject[QStringLiteral("BadgeTextOnly")].toBool());
        session->setBadgeTransparency(sessionObject[QStringLiteral("BadgeTransparency")].toInt());
    }
}

ViewSplitter *restoreSessionsSplitterRecurse(const QJsonObject &jsonSplitter, ViewManager *manager, bool useSessionId)
{
    const QJsonArray splitterWidgets = jsonSplitter[QStringLiteral("Widgets")].toArray();
    auto orientation = (jsonSplitter[QStringLiteral("Orientation")].toString() == QStringLiteral("Horizontal")) ? Qt::Horizontal : Qt::Vertical;

    auto *currentSplitter = new ViewSplitter();
    currentSplitter->setOrientation(orientation);

    for (const auto widgetJsonValue : splitterWidgets) {
        const auto widgetJsonObject = widgetJsonValue.toObject();
        const auto sessionIterator = widgetJsonObject.constFind(QStringLiteral("SessionRestoreId"));
        const auto columnsIterator = widgetJsonObject.constFind(QStringLiteral("Columns"));
        const auto linesIterator = widgetJsonObject.constFind(QStringLiteral("Lines"));
        const auto commandIterator = widgetJsonObject.constFind(QStringLiteral("Command"));
        const auto cwdIterator = widgetJsonObject.constFind(QStringLiteral("WorkingDirectory"));

        if (sessionIterator != widgetJsonObject.constEnd()) {
            Session *session = useSessionId ? SessionManager::instance()->idToSession(sessionIterator->toInt())
                                            : manager->createSession(savedSessionProfile(widgetJsonObject));

            if (!useSessionId) {
                restoreColdSessionState(session, widgetJsonObject);
            }

            auto newView = manager->createView(session);
            currentSplitter->addTerminalDisplay(newView, -1);

            int columns = newView->columns();
            int lines = newView->lines();
            if (columnsIterator != widgetJsonObject.constEnd()) {
                columns = columnsIterator->toInt();
            }
            if (linesIterator != widgetJsonObject.constEnd()) {
                lines = linesIterator->toInt();
            }
            newView->setSize(columns, lines);

            // Set the current working directory if the key is not empty
            if (cwdIterator != widgetJsonObject.constEnd()) {
                auto cwd = cwdIterator->toString();
                if (!cwd.isEmpty()) {
                    newView->session()->setInitialWorkingDirectory(cwd);
                }
            }

            if (!newView->session()->isRunning()) {
                newView->session()->run();
            }

            if (useSessionId && commandIterator != widgetJsonObject.constEnd()) {
                auto command = commandIterator->toString();
                // Don't open a program that is already running, such as bash
                if (!command.isEmpty() && command != newView->session()->program()) {
                    newView->session()->runCommandFromLayout(command);
                }
            }

        } else {
            auto nextSplitter = restoreSessionsSplitterRecurse(widgetJsonObject, manager, useSessionId);
            currentSplitter->addWidget(nextSplitter);
        }
    }
    return currentSplitter;
}

} // namespace

namespace
{
void restoreTabsIntoContainer(ViewManager *manager, TabbedViewContainer *container, const QJsonArray &jsonTabs, int activeTab, bool useSessionIds)
{
    for (const auto &jsonSplitter : jsonTabs) {
        auto topLevelSplitter = restoreSessionsSplitterRecurse(jsonSplitter.toObject(), manager, useSessionIds);
        container->addSplitter(topLevelSplitter, container->count());
    }

    if (jsonTabs.isEmpty()) {
        Profile::Ptr profile = ProfileManager::instance()->defaultProfile();
        Session *session = SessionManager::instance()->createSession(profile);
        container->addView(manager->createView(session));
        if (!session->isRunning()) {
            session->run();
        }
    }

    if (container->count() > 0) {
        container->setCurrentIndex(qBound(0, activeTab, container->count() - 1));
    }
}

}

void ViewManager::loadLayout(QString file)
{
    // User pressed cancel in dialog
    if (file.isEmpty()) {
        return;
    }

    QFile jsonFile(file);

    if (!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        KMessageBox::error(this->widget(), i18nc("@label:textbox", "A problem occurred when loading the Layout.\n%1", jsonFile.fileName()));
    }
    auto json = QJsonDocument::fromJson(jsonFile.readAll());
    if (!json.isEmpty()) {
        auto splitter = restoreSessionsSplitterRecurse(json.object(), this, false);
        activeContainer()->addSplitter(splitter, activeContainer()->count());
    }
}
void ViewManager::loadLayoutFile()
{
    loadLayout(QFileDialog::getOpenFileName(this->widget(),
                                            i18nc("@title:window", "Load Tab Layout"),
                                            QStringLiteral("~/"),
                                            i18nc("@item:inlistbox", "Konsole View Layout (*.json)")));
}

void ViewManager::restoreSessions(const KConfigGroup &group)
{
    restoreSessions(group, true);
}

void ViewManager::restoreSessions(const KConfigGroup &group, bool useSessionIds)
{
    if (!_workspaceContainer.isNull() && group.hasKey(QStringLiteral("ProjectRailWidth"))) {
        _workspaceContainer->setProjectRailWidth(group.readEntry("ProjectRailWidth", 0));
    }

    const auto projectList = group.readEntry("Projects", QByteArray("[]"));
    const auto jsonProjects = QJsonDocument::fromJson(projectList).array();
    if (!_workspaceContainer.isNull() && !jsonProjects.isEmpty()) {
        QList<TabbedViewContainer *> restoredContainers;
        restoredContainers.reserve(jsonProjects.count());

        bool reusedInitialProject = false;
        for (const auto &projectValue : jsonProjects) {
            const auto projectObject = projectValue.toObject();
            const QString title = projectObject[QStringLiteral("Title")].toString(_workspaceContainer->nextDefaultProjectTitle());

            TabbedViewContainer *container = nullptr;
            if (!reusedInitialProject && _workspaceContainer->projectCount() == 1 && activeContainer() != nullptr && activeContainer()->count() == 0) {
                container = activeContainer();
                _workspaceContainer->setProjectTitle(container, title);
                reusedInitialProject = true;
            } else {
                container = createContainer();
                _workspaceContainer->addProject(container, title);
            }

            const auto tabs = projectObject[QStringLiteral("Tabs")].toArray();
            const int activeTab = projectObject[QStringLiteral("Active")].toInt(0);
            restoreTabsIntoContainer(this, container, tabs, activeTab, useSessionIds);
            restoredContainers.append(container);
        }

        const int activeProject = qBound(0, group.readEntry("ActiveProject", 0), restoredContainers.count() - 1);
        _workspaceContainer->activateProject(restoredContainers.at(activeProject));
        return;
    }

    const auto tabList = group.readEntry("Tabs", QByteArray("[]"));
    const auto jsonTabs = QJsonDocument::fromJson(tabList).array();
    for (const auto &jsonSplitter : jsonTabs) {
        auto topLevelSplitter = restoreSessionsSplitterRecurse(jsonSplitter.toObject(), this, useSessionIds);
        activeContainer()->addSplitter(topLevelSplitter, activeContainer()->count());
    }

    if (!jsonTabs.isEmpty() || !useSessionIds)
        return;

    // Session file is unusable, try older format
    QList<int> ids = group.readEntry("Sessions", QList<int>());
    int activeTab = group.readEntry("Active", 0);
    TerminalDisplay *display = nullptr;

    int tab = 1;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        const int &id = *it;
        Session *session = SessionManager::instance()->idToSession(id);

        if (session == nullptr) {
            qWarning() << "Unable to load session with id" << id;
            // Force a creation of a default session below
            ids.clear();
            break;
        }

        activeContainer()->addView(createView(session));
        if (!session->isRunning()) {
            session->run();
        }
        if (tab++ == activeTab) {
            display = qobject_cast<TerminalDisplay *>(activeView());
        }
    }

    if (display != nullptr) {
        if (auto *splitter = ViewSplitter::parentSplitterForDisplay(display); splitter != nullptr) {
            activeContainer()->setCurrentWidget(splitter->getToplevelSplitter());
        }
        display->setFocus(Qt::OtherFocusReason);
    }

    if (ids.isEmpty()) { // Session file is unusable, start default Profile
        Profile::Ptr profile = ProfileManager::instance()->defaultProfile();
        Session *session = SessionManager::instance()->createSession(profile);
        activeContainer()->addView(createView(session));
        if (!session->isRunning()) {
            session->run();
        }
    }
}

void ViewManager::initializeRestoredSessions()
{
    if (_workspaceContainer.isNull()) {
        return;
    }

    const QList<TabbedViewContainer *> containers = _workspaceContainer->containers();
    QHash<TabbedViewContainer *, int> activeTabs;
    activeTabs.reserve(containers.count());
    for (auto *container : containers) {
        activeTabs.insert(container, container->currentIndex());
    }
    auto *activeProject = _workspaceContainer->activeContainer();

    for (auto *container : containers) {
        for (int tab = 0; tab < container->count(); ++tab) {
            container->setCurrentIndex(tab);
        }
    }

    for (auto *container : containers) {
        const int activeTab = activeTabs.value(container, 0);
        if (container->count() > 0) {
            container->setCurrentIndex(qBound(0, activeTab, container->count() - 1));
        }
    }
    if (activeProject != nullptr) {
        _workspaceContainer->activateProject(activeProject);
    }
}

TabbedViewContainer *ViewManager::activeContainer() const
{
    return _workspaceContainer != nullptr ? _workspaceContainer->activeContainer() : nullptr;
}

int ViewManager::sessionCount()
{
    return sessionList().count();
}

QStringList ViewManager::sessionList()
{
    QStringList ids;

    auto *container = activeContainer();
    for (int i = 0; container != nullptr && i < container->count(); i++) {
        const auto terminaldisplayList = container->widget(i)->findChildren<TerminalDisplay *>();
        for (auto *terminaldisplay : terminaldisplayList) {
            ids.append(QString::number(terminaldisplay->sessionController()->session()->sessionId()));
        }
    }

    return ids;
}

int ViewManager::currentSession()
{
    if (_pluggedController != nullptr) {
        Q_ASSERT(_pluggedController->session() != nullptr);
        return _pluggedController->session()->sessionId();
    }
    return -1;
}

void ViewManager::setCurrentSession(int sessionId)
{
    auto *session = SessionManager::instance()->idToSession(sessionId);
    if (session == nullptr || session->views().count() == 0) {
        return;
    }

    auto *display = session->views().at(0);
    if (display != nullptr) {
        display->setFocus(Qt::OtherFocusReason);

        auto *splitter = ViewSplitter::parentSplitterForDisplay(display);
        if (splitter != nullptr) {
            if (auto *container = containerForTerminal(display)) {
                _workspaceContainer->activateProject(container);
                container->setCurrentWidget(splitter->getToplevelSplitter());
            }
        }
    }
}

int ViewManager::newSession()
{
    return newSession(QString(), QString());
}

int ViewManager::newSession(const QString &profile)
{
    return newSession(profile, QString());
}

int ViewManager::newSession(const QString &profile, const QString &directory)
{
    Profile::Ptr profileptr = ProfileManager::instance()->defaultProfile();
    if (!profile.isEmpty()) {
        const QList<Profile::Ptr> profilelist = ProfileManager::instance()->allProfiles();

        for (const auto &i : profilelist) {
            if (i->name() == profile) {
                profileptr = i;
                break;
            }
        }
    }

    Session *session = createSession(profileptr, directory);

    auto newView = createView(session);
    activeContainer()->addView(newView);
    session->run();

    return session->sessionId();
}

QString ViewManager::defaultProfile()
{
    return ProfileManager::instance()->defaultProfile()->name();
}

void ViewManager::setDefaultProfile(const QString &profileName)
{
    const QList<Profile::Ptr> profiles = ProfileManager::instance()->allProfiles();
    for (const Profile::Ptr &profile : profiles) {
        if (profile->name() == profileName) {
            ProfileManager::instance()->setDefaultProfile(profile);
        }
    }
}

QStringList ViewManager::profileList()
{
    return ProfileManager::instance()->availableProfileNames();
}

void ViewManager::nextSession()
{
    nextView();
}

void ViewManager::prevSession()
{
    previousView();
}

void ViewManager::moveSessionLeft()
{
    moveActiveViewLeft();
}

void ViewManager::moveSessionRight()
{
    moveActiveViewRight();
}

void ViewManager::setTabWidthToText(bool setTabWidthToText)
{
    activeContainer()->tabBar()->setExpanding(!setTabWidthToText);
    activeContainer()->tabBar()->update();
}

QStringList ViewManager::viewHierarchy()
{
    QStringList list;

    auto *container = activeContainer();
    for (int i = 0; container != nullptr && i < container->count(); ++i) {
        list.append(container->viewSplitterAt(i)->getChildWidgetsLayout());
    }

    return list;
}

QList<double> ViewManager::getSplitProportions(int splitterId)
{
    const auto *splitter = activeContainer()->findSplitter(splitterId);
    if (splitter == nullptr)
        return QList<double>();

    const QList<int> sizes = splitter->sizes();
    int totalSize = 0;

    for (const auto &size : sizes) {
        totalSize += size;
    }

    QList<double> percentages;
    if (totalSize == 0)
        return percentages;

    for (auto size : sizes) {
        percentages.append((size / static_cast<double>(totalSize)) * 100);
    }

    return percentages;
}

bool ViewManager::createSplit(int viewId, bool horizontalSplit)
{
    if (auto view = findTerminalDisplay(viewId)) {
        setCurrentView(view);
        splitView(horizontalSplit ? Qt::Horizontal : Qt::Vertical, false);
        return true;
    }

    return false;
}

bool ViewManager::createSplitWithExisting(int targetSplitterId, QStringList widgetInfos, int idx, bool horizontalSplit)
{
    auto *container = activeContainer();
    auto targetSplitter = container->findSplitter(targetSplitterId);
    if (targetSplitter == nullptr || idx < 0)
        return false;

    QVector<QWidget *> linearLayout;
    QList<int> forbiddenSplitters, forbiddenViews;

    // specify that top level splitters should not be used as children for created splittter
    for (int i = 0; i < container->count(); ++i) {
        forbiddenSplitters.append(container->viewSplitterAt(i)->id());
    }

    // specify that parent splitters of the splitter with targetSplitterId id should not be used
    // as children for created splitter
    for (auto splitter = targetSplitter; splitter != targetSplitter->getToplevelSplitter(); splitter = qobject_cast<ViewSplitter *>(splitter->parentWidget())) {
        forbiddenSplitters.append(splitter->id());
    }

    // to make positioning clearer by avoiding situations where
    // e.g. splitter to be created is at index x of targetSplitter
    // and some direct children of targetSplitter are used as
    // children of created splitter, causing the final position
    // of created splitter to may not be at x
    for (int i = 0; i < targetSplitter->count(); ++i) {
        auto w = targetSplitter->widget(i);

        if (auto s = qobject_cast<ViewSplitter *>(w))
            forbiddenSplitters.append(s->id());
        else if (auto *terminal = ViewSplitter::terminalDisplayForWidget(w))
            forbiddenViews.append(terminal->id());
    }

    for (auto &info : widgetInfos) {
        auto typeAndId = info.split(QLatin1Char('-'));
        if (typeAndId.size() != 2)
            return false;

        int id = typeAndId[1].toInt();
        QChar type = typeAndId[0][0];

        if (type == QLatin1Char('s') && !forbiddenSplitters.removeOne(id)) {
            if (auto s = container->findSplitter(id)) {
                linearLayout.append(s);
                continue;
            }
        } else if (type == QLatin1Char('v') && !forbiddenViews.removeOne(id)) {
            if (auto v = findTerminalDisplay(id)) {
                linearLayout.append(v);
                continue;
            }
        }

        return false;
    }

    if (linearLayout.count() == 1) {
        if (auto onlyChildSplitter = qobject_cast<ViewSplitter *>(linearLayout[0])) {
            targetSplitter->addSplitter(onlyChildSplitter, idx);
        } else {
            auto onlyChildView = qobject_cast<TerminalDisplay *>(linearLayout[0]);
            targetSplitter->addTerminalDisplay(onlyChildView, idx);
        }
    } else {
        ViewSplitter *createdSplitter = new ViewSplitter();
        createdSplitter->setOrientation(horizontalSplit ? Qt::Horizontal : Qt::Vertical);

        for (auto widget : std::as_const(linearLayout)) {
            if (auto s = qobject_cast<ViewSplitter *>(widget))
                createdSplitter->addSplitter(s);
            else
                createdSplitter->addTerminalDisplay(qobject_cast<TerminalDisplay *>(widget));
        }

        targetSplitter->addSplitter(createdSplitter, idx);
    }

    setCurrentView(targetSplitter->activeTerminalDisplay());
    return true;
}

bool ViewManager::setCurrentView(int viewId)
{
    if (auto view = findTerminalDisplay(viewId)) {
        setCurrentView(view);
        return true;
    }

    return false;
}

bool ViewManager::resizeSplits(int splitterId, QList<double> percentages)
{
    auto splitter = activeContainer()->findSplitter(splitterId);
    int totalP = 0;

    for (auto p : percentages) {
        if (p < 1)
            return false;

        totalP += p;
    }

    // make sure that the sum of percentages is very close
    // to but not exceeding 100. above 99% but less than 100 %
    // seems like good constraint
    if (splitter == nullptr || percentages.count() != splitter->sizes().count() || totalP > 100 || totalP < 99)
        return false;

    int sum = 0;
    QList<int> newSizes;

    const auto sizes = splitter->sizes();
    for (int size : sizes) {
        sum += size;
    }

    for (int i = 0; i < percentages.count(); ++i) {
        newSizes.append(static_cast<int>(sum * percentages.at(i)));
    }

    splitter->setSizes(newSizes);
    setCurrentView(splitter->activeTerminalDisplay());
    return true;
}

bool ViewManager::moveSplitter(int splitterId, int targetSplitterId, int idx)
{
    auto *container = activeContainer();
    auto splitter = container->findSplitter(splitterId);
    auto targetSplitter = container->findSplitter(targetSplitterId);

    if (splitter == nullptr || targetSplitter == nullptr || idx < 0)
        return false;

    for (auto s = targetSplitter; s != s->getToplevelSplitter(); s = qobject_cast<ViewSplitter *>(s->parentWidget())) {
        if (s == splitter)
            return false;
    }

    for (int i = 0; i < container->count(); ++i) {
        if (splitter == container->viewSplitterAt(i))
            return false;
    }

    targetSplitter->addSplitter(splitter, idx);
    setCurrentView(splitter->activeTerminalDisplay());
    return true;
}

bool ViewManager::moveView(int viewId, int targetSplitterId, int idx)
{
    auto view = findTerminalDisplay(viewId);
    auto targetSplitter = activeContainer()->findSplitter(targetSplitterId);

    if (view == nullptr || targetSplitter == nullptr || idx < 0)
        return false;

    targetSplitter->addTerminalDisplay(view, idx);
    setCurrentView(view);
    return true;
}

void ViewManager::setNavigationVisibility(NavigationVisibility navigationVisibility)
{
    if (_navigationVisibility != navigationVisibility) {
        _navigationVisibility = navigationVisibility;
        for (auto *container : _workspaceContainer->containers()) {
            container->setNavigationVisibility(navigationVisibility);
        }
    }
}

void ViewManager::updateTerminalDisplayHistory(TerminalDisplay *terminalDisplay, bool remove)
{
    if (terminalDisplay == nullptr) {
        if (_terminalDisplayHistoryIndex >= 0) {
            // This is the case when we finished walking through the history
            // (i.e. when Ctrl-Tab has been released)
            terminalDisplay = _terminalDisplayHistory[_terminalDisplayHistoryIndex];
            _terminalDisplayHistoryIndex = -1;
        } else {
            return;
        }
    }

    if (_terminalDisplayHistoryIndex >= 0 && !remove) {
        // Do not reorder the tab history while we are walking through it
        return;
    }

    for (int i = 0; i < _terminalDisplayHistory.count(); i++) {
        if (_terminalDisplayHistory[i] == terminalDisplay) {
            _terminalDisplayHistory.removeAt(i);
            if (!remove) {
                _terminalDisplayHistory.prepend(terminalDisplay);
            }
            break;
        }
    }
}

TabbedViewContainer *ViewManager::containerForTerminal(TerminalDisplay *terminal) const
{
    if (terminal == nullptr || _workspaceContainer.isNull()) {
        return nullptr;
    }

    auto *splitter = ViewSplitter::parentSplitterForDisplay(terminal);
    if (splitter == nullptr) {
        return nullptr;
    }

    return _workspaceContainer->containerForWidget(splitter->getToplevelSplitter());
}

void ViewManager::registerTerminal(TerminalDisplay *terminal, TabbedViewContainer *container)
{
    if (container == nullptr) {
        container = activeContainer();
    }
    if (terminal == nullptr || container == nullptr) {
        return;
    }

    connect(terminal, &TerminalDisplay::requestToggleExpansion, container, &TabbedViewContainer::toggleMaximizeCurrentTerminal, Qt::UniqueConnection);
    connect(terminal, &TerminalDisplay::requestMoveToNewTab, container, &TabbedViewContainer::moveToNewTab, Qt::UniqueConnection);
}

void ViewManager::unregisterTerminal(TerminalDisplay *terminal)
{
    disconnect(terminal, &TerminalDisplay::requestToggleExpansion, nullptr, nullptr);
    disconnect(terminal, &TerminalDisplay::requestMoveToNewTab, nullptr, nullptr);
}

void ViewManager::markSessionAttention(Session *session, TabbedViewContainer *container)
{
    if (session == nullptr || container == nullptr || container == activeContainer()) {
        return;
    }

    _sessionsNeedingAttention.insert(session);
}

void ViewManager::clearProjectAttention(TabbedViewContainer *container)
{
    const auto controllers = sessionControllersForContainer(container);
    for (SessionController *controller : controllers) {
        if (controller != nullptr) {
            _sessionsNeedingAttention.remove(controller->session());
        }
    }
}

void ViewManager::setSessionProjectStatus(Session *session,
                                          TabbedViewContainer *container,
                                          const QString &status,
                                          qlonglong agentProcessId,
                                          const QString &agent,
                                          const QString &event)
{
    if (session == nullptr) {
        return;
    }

    const auto projectStatus = projectStatusFromString(status);
    if (projectStatus == ProjectWorkspaceContainer::ProjectStatus::None) {
        _sessionProjectStatuses.remove(session);
        _sessionsNeedingAttention.remove(session);
        updateProjectStatusProcessTimer();
        return;
    }

    const auto previousStatus = _sessionProjectStatuses.value(session);
    const bool agentProcessChanged = agentProcessId > 0 && previousStatus.agentProcessId > 0 && agentProcessId != previousStatus.agentProcessId;
    if (agentProcessId <= 0) {
        agentProcessId = previousStatus.agentProcessId;
    }

    const bool isCodexEvent = agent.compare(QLatin1String("codex"), Qt::CaseInsensitive) == 0;
    const bool isPermissionRequest = event.compare(QLatin1String("PermissionRequest"), Qt::CaseInsensitive) == 0;
    const bool resetsPendingDecisions = event.compare(QLatin1String("SessionStart"), Qt::CaseInsensitive) == 0
        || event.compare(QLatin1String("UserPromptSubmit"), Qt::CaseInsensitive) == 0 || event.compare(QLatin1String("Stop"), Qt::CaseInsensitive) == 0;

    int pendingTerminalDecisions = agentProcessChanged || !isCodexEvent || resetsPendingDecisions ? 0 : previousStatus.pendingTerminalDecisions;
    if (isCodexEvent && isPermissionRequest) {
        ++pendingTerminalDecisions;
    }

    const auto effectiveStatus = pendingTerminalDecisions > 0 ? ProjectWorkspaceContainer::ProjectStatus::NeedsInput : projectStatus;
    _sessionProjectStatuses.insert(session, {effectiveStatus, agentProcessId, pendingTerminalDecisions});
    if (effectiveStatus == ProjectWorkspaceContainer::ProjectStatus::NeedsInput) {
        markSessionAttention(session, container);
    } else {
        _sessionsNeedingAttention.remove(session);
    }
    updateProjectStatusProcessTimer();
}

void ViewManager::handleSessionTerminalDecisionKey(Session *session, TabbedViewContainer *container, QKeyEvent *keyEvent)
{
    if (session == nullptr || keyEvent == nullptr || keyEvent->type() != QEvent::KeyPress || keyEvent->isAutoRepeat()) {
        return;
    }

    const auto commandModifiers = keyEvent->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool resolvesDecision =
        commandModifiers == Qt::NoModifier && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Escape);
    auto status = _sessionProjectStatuses.find(session);
    if (!resolvesDecision || status == _sessionProjectStatuses.end() || status->pendingTerminalDecisions <= 0
        || status->status != ProjectWorkspaceContainer::ProjectStatus::NeedsInput) {
        return;
    }

    --status->pendingTerminalDecisions;
    if (status->pendingTerminalDecisions == 0) {
        status->status = ProjectWorkspaceContainer::ProjectStatus::Running;
        _sessionsNeedingAttention.remove(session);
    }
    refreshProjectSummary(container);
}

void ViewManager::clearExitedSessionProjectStatuses()
{
    QSet<TabbedViewContainer *> containersToRefresh;
    for (auto status = _sessionProjectStatuses.begin(); status != _sessionProjectStatuses.end();) {
        if (status->agentProcessId <= 0 || projectStatusProcessIsAlive(status->agentProcessId)) {
            ++status;
            continue;
        }

        Session *session = status.key();
        _sessionsNeedingAttention.remove(session);
        for (auto terminal = _sessionMap.cbegin(); terminal != _sessionMap.cend(); ++terminal) {
            if (terminal.value() == session) {
                if (auto *container = containerForTerminal(terminal.key())) {
                    containersToRefresh.insert(container);
                }
            }
        }
        status = _sessionProjectStatuses.erase(status);
    }

    updateProjectStatusProcessTimer();
    for (TabbedViewContainer *container : std::as_const(containersToRefresh)) {
        refreshProjectSummary(container);
    }
}

void ViewManager::updateProjectStatusProcessTimer()
{
    const bool hasTrackedProcess = std::any_of(_sessionProjectStatuses.cbegin(), _sessionProjectStatuses.cend(), [](const SessionProjectStatus &status) {
        return status.agentProcessId > 0;
    });
    if (hasTrackedProcess) {
        if (!_projectStatusProcessTimer.isActive()) {
            _projectStatusProcessTimer.start();
        }
    } else {
        _projectStatusProcessTimer.stop();
    }
}

void ViewManager::updateProjectInputRequirement()
{
    const bool hasProjectNeedingInput = !_workspaceContainer.isNull() && _workspaceContainer->projectModel()->hasProjectNeedingInput();
    if (_hasProjectNeedingInput == hasProjectNeedingInput) {
        return;
    }

    _hasProjectNeedingInput = hasProjectNeedingInput;
    Q_EMIT updateWindowIcon();
}

void ViewManager::addMoveTabToProjectMenu(QMenu *menu, TabbedViewContainer *sourceContainer, int tabIndex)
{
    if (_navigationMethod == NoNavigation || menu == nullptr || sourceContainer == nullptr || _workspaceContainer.isNull()) {
        return;
    }

    auto *projectMenu = menu->addMenu(QIcon::fromTheme(QStringLiteral("go-jump")), i18nc("@action:inmenu", "Move Tab to Project"));
    projectMenu->menuAction()->setObjectName(QStringLiteral("move-tab-to-project"));
    projectMenu->setEnabled(tabIndex >= 0 && tabIndex < sourceContainer->count() && _workspaceContainer->projectCount() > 1);

    const auto containers = _workspaceContainer->containers();
    for (TabbedViewContainer *targetContainer : containers) {
        if (targetContainer == nullptr || targetContainer == sourceContainer) {
            continue;
        }

        const QString title = _workspaceContainer->projectTitle(targetContainer);
        auto *action = projectMenu->addAction(title.isEmpty() ? i18nc("@title", "Project") : title, this, [this, sourceContainer, targetContainer, tabIndex] {
            moveTabToProject(sourceContainer, tabIndex, targetContainer);
        });
        action->setEnabled(tabIndex >= 0 && tabIndex < sourceContainer->count());
    }
}

void ViewManager::moveTabToProject(TabbedViewContainer *sourceContainer, int tabIndex, TabbedViewContainer *targetContainer)
{
    if (sourceContainer == nullptr || targetContainer == nullptr || sourceContainer == targetContainer || tabIndex < 0
        || tabIndex >= sourceContainer->count()) {
        return;
    }

    auto *splitter = sourceContainer->viewSplitterAt(tabIndex);
    if (splitter == nullptr) {
        return;
    }

    const auto terminals = splitter->findChildren<TerminalDisplay *>();
    sourceContainer->moveTabToContainer(tabIndex, targetContainer);
    for (TerminalDisplay *terminal : terminals) {
        unregisterTerminal(terminal);
        registerTerminal(terminal, targetContainer);
    }

    if (!_workspaceContainer.isNull()) {
        _workspaceContainer->activateProject(targetContainer);
    }
    refreshProjectSummary(sourceContainer);
    refreshProjectSummary(targetContainer);
    toggleActionsBasedOnState();
    Q_EMIT viewPropertiesChanged(viewProperties());
}

void ViewManager::refreshProjectSummary(TabbedViewContainer *container)
{
    if (container == nullptr || _workspaceContainer.isNull()) {
        return;
    }

    QString activeTitle;
    QString activeDirectory;
    QIcon activeIcon = QIcon::fromTheme(QStringLiteral("folder"));

    if (auto *splitter = container->activeViewSplitter()) {
        if (auto *terminal = splitter->activeTerminalDisplay()) {
            if (auto *controller = terminal->sessionController()) {
                activeTitle = controller->title();
                activeDirectory = controller->currentDir();
                if (!controller->icon().isNull()) {
                    activeIcon = controller->icon();
                }
            }
        }
    }

    if (!activeDirectory.isEmpty()) {
        const QString homePath = QDir::homePath();
        if (activeDirectory == homePath) {
            activeDirectory = QStringLiteral("~");
        } else if (activeDirectory.startsWith(homePath + QLatin1Char('/'))) {
            activeDirectory = QStringLiteral("~/") + activeDirectory.mid(homePath.length() + 1);
        }
    }

    QString subtitle;
    if (!activeTitle.isEmpty() && !activeDirectory.isEmpty()) {
        subtitle = i18nc("@info:project active tab and path", "%1 - %2", activeTitle, activeDirectory);
    } else if (!activeTitle.isEmpty()) {
        subtitle = activeTitle;
    } else {
        subtitle = activeDirectory;
    }

    int activeProcessCount = 0;
    bool hasActivity = false;
    auto projectStatus = ProjectWorkspaceContainer::ProjectStatus::None;
    QSet<Session *> seenSessions;
    const auto terminals = container->findChildren<TerminalDisplay *>();
    for (TerminalDisplay *terminal : terminals) {
        Session *session = _sessionMap.value(terminal);
        if (session == nullptr || seenSessions.contains(session)) {
            continue;
        }

        seenSessions.insert(session);
        hasActivity = hasActivity || session->activeNotifications() != Session::NoNotification || _sessionsNeedingAttention.contains(session);
        projectStatus = higherPriorityProjectStatus(projectStatus, _sessionProjectStatuses.value(session).status);
        if (!session->isRunning() || !session->isForegroundProcessActive()) {
            continue;
        }

        const QString defaultProcess = QFileInfo(session->program()).fileName();
        const QString currentProcess = QFileInfo(session->foregroundProcessName()).fileName();
        if (!currentProcess.isEmpty() && currentProcess != defaultProcess) {
            ++activeProcessCount;
        }
    }

    _workspaceContainer->setProjectSummary(container, subtitle, container->count(), activeProcessCount, hasActivity, projectStatus, activeIcon);
}

#include "moc_ViewManager.cpp"
